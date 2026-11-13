#include "coaster_domain.h"
#include <cmath>

CoasterDomain::CoasterDomain(float daily_goal_ml) {
    status_.cup_state = CupState::EMPTY;
    status_.alert_state = AlertState::IDLE;
    status_.raw_weight_g = 0.0f;
    status_.tare_offset_g = 0.0f;
    status_.net_weight_g = 0.0f;
    status_.stable_weight_g = 0.0f;
    status_.baseline_weight_g = 0.0f;
    status_.consumed_today_ml = 0.0f;
    status_.daily_goal_ml = daily_goal_ml;
    status_.progress_pct = 0.0f;
    status_.drink_count = 0;
    status_.reminder_remaining_ms = REMINDER_INTERVAL_MS_NORMAL;
    status_.countdown_s = REMINDER_INTERVAL_MS_NORMAL / 1000;
    status_.is_stable = false;
    status_.alert_active = false;
    status_.demo_mode = false;
    tap_active_ = false;
    tap_start_ms_ = 0;
    tap_peak_g_ = 0.0f;
    tap_count_ = 0;
    wifi_ap_trigger_pending_ = false;
    for (size_t i = 0; i < TAP_REQUIRED_COUNT; ++i) {
        tap_times_[i] = 0;
    }
}

void CoasterDomain::update(float raw_weight_g, uint32_t now_ms) {
    current_time_ms_ = now_ms;
    status_.raw_weight_g = raw_weight_g;
    status_.net_weight_g = raw_weight_g - status_.tare_offset_g;

    record_stability_sample(now_ms, status_.net_weight_g);
    evaluate_stability(now_ms);

    tick_state_machine(now_ms);
    tick_reminder_timers(now_ms);
    evaluate_tap_gesture(now_ms, status_.net_weight_g);
}

void CoasterDomain::record_stability_sample(uint32_t now_ms, float net_weight) {
    stability_buffer_[stability_head_] = { now_ms, net_weight };
    stability_head_ = (stability_head_ + 1) % STABILITY_BUFFER_CAPACITY;
    if (stability_count_ < STABILITY_BUFFER_CAPACITY) {
        stability_count_++;
    } else {
        stability_tail_ = (stability_tail_ + 1) % STABILITY_BUFFER_CAPACITY;
    }
}

void CoasterDomain::evaluate_stability(uint32_t now_ms) {
    // Retain boundary anchor sample spanning the 1500ms window:
    // Only purge tail sample if the next sample already spans the required window
    while (stability_count_ > 1) {
        size_t next_idx = (stability_tail_ + 1) % STABILITY_BUFFER_CAPACITY;
        uint32_t next_age = now_ms - stability_buffer_[next_idx].time_ms;
        if (next_age >= STABILITY_WINDOW_MS) {
            stability_tail_ = next_idx;
            stability_count_--;
        } else {
            break;
        }
    }

    if (stability_count_ < STABILITY_MIN_SAMPLES) {
        status_.is_stable = false;
        status_.stable_weight_g = status_.net_weight_g;
        return;
    }

    uint32_t oldest_t = stability_buffer_[stability_tail_].time_ms;
    uint32_t duration = now_ms - oldest_t;

    float min_w = stability_buffer_[stability_tail_].weight_g;
    float max_w = min_w;
    float sum_w = 0.0f;

    for (size_t i = 0; i < stability_count_; ++i) {
        size_t idx = (stability_tail_ + i) % STABILITY_BUFFER_CAPACITY;
        float w = stability_buffer_[idx].weight_g;
        if (w < min_w) min_w = w;
        if (w > max_w) max_w = w;
        sum_w += w;
    }

    float span = max_w - min_w;

    // Tolerance 0.0005f handles IEEE-754 precision boundary
    if (duration >= STABILITY_WINDOW_MS && span <= (STABILITY_MAX_SPAN_G + 0.0005f)) {
        status_.is_stable = true;
        status_.stable_weight_g = sum_w / static_cast<float>(stability_count_);
    } else {
        status_.is_stable = false;
        status_.stable_weight_g = status_.net_weight_g;
    }
}

void CoasterDomain::clear_stability_buffer() {
    stability_head_ = 0;
    stability_tail_ = 0;
    stability_count_ = 0;
    status_.is_stable = false;
    status_.stable_weight_g = status_.net_weight_g;
}

void CoasterDomain::tick_state_machine(uint32_t now_ms) {
    float net_w = status_.net_weight_g;

    if (status_.cup_state == CupState::EMPTY) {
        // Auto-tare logic: strictly when unoccupied and drift is within [0.05g, 5.0g]
        if (std::abs(net_w) <= AUTO_TARE_WINDOW_G && std::abs(net_w) >= AUTO_TARE_MIN_DRIFT_G) {
            if (!auto_tare_active_) {
                auto_tare_active_ = true;
                auto_tare_start_ms_ = now_ms;
            } else if (now_ms - auto_tare_start_ms_ >= get_auto_tare_time_ms()) {
                status_.tare_offset_g += net_w;
                status_.net_weight_g = 0.0f;
                status_.stable_weight_g = 0.0f;
                auto_tare_active_ = false;
                clear_stability_buffer();
                emit_event(DomainEventType::AUTO_TARE_APPLIED, now_ms, CupState::EMPTY, CupState::EMPTY, status_.tare_offset_g, 0.0f);
            }
        } else {
            auto_tare_active_ = false;
        }

        // Cup presence transition (> 30.0g)
        if (net_w > CUP_PRESENCE_MIN_G) {
            CupState old_st = status_.cup_state;
            status_.cup_state = CupState::STABILIZING;
            clear_stability_buffer();
            auto_tare_active_ = false;
            emit_event(DomainEventType::STATE_CHANGE, now_ms, old_st, status_.cup_state);
        }
    } else if (status_.cup_state == CupState::STABILIZING) {
        if (net_w <= CUP_PRESENCE_MIN_G) {
            // Snatched before settling -> revert to EMPTY
            CupState old_st = status_.cup_state;
            status_.cup_state = CupState::EMPTY;
            clear_stability_buffer();
            emit_event(DomainEventType::STATE_CHANGE, now_ms, old_st, status_.cup_state);
        } else if (status_.is_stable && status_.stable_weight_g > CUP_PRESENCE_MIN_G) {
            // Stable cup presence confirmed
            CupState old_st = status_.cup_state;
            status_.cup_state = CupState::CUP_PRESENT;
            status_.baseline_weight_g = status_.stable_weight_g;
            status_.alert_state = AlertState::COUNTING;
            last_drink_time_ms_ = now_ms;
            session_active_ = true;
            emit_event(DomainEventType::STATE_CHANGE, now_ms, old_st, status_.cup_state, status_.baseline_weight_g);
            emit_event(DomainEventType::CUP_PLACED, now_ms, old_st, status_.cup_state, status_.baseline_weight_g);
        }
    } else if (status_.cup_state == CupState::CUP_PRESENT) {
        if (net_w < CUP_LIFT_MAX_G) {
            // Cup lifted for drinking or refill (< 20.0g)
            CupState old_st = status_.cup_state;
            status_.cup_state = CupState::CUP_LIFTED;
            clear_stability_buffer();
            emit_event(DomainEventType::STATE_CHANGE, now_ms, old_st, status_.cup_state);
            emit_event(DomainEventType::CUP_LIFTED, now_ms, old_st, status_.cup_state);
        }
    } else if (status_.cup_state == CupState::CUP_LIFTED) {
        if (net_w > CUP_PRESENCE_MIN_G) {
            // Cup returned to stand (> 30.0g)
            CupState old_st = status_.cup_state;
            status_.cup_state = CupState::RETURN_STABILIZING;
            clear_stability_buffer();
            emit_event(DomainEventType::STATE_CHANGE, now_ms, old_st, status_.cup_state);
        }
    } else if (status_.cup_state == CupState::RETURN_STABILIZING) {
        if (net_w < CUP_LIFT_MAX_G) {
            // Cup lifted again before settling
            CupState old_st = status_.cup_state;
            status_.cup_state = CupState::CUP_LIFTED;
            clear_stability_buffer();
            emit_event(DomainEventType::STATE_CHANGE, now_ms, old_st, status_.cup_state);
        } else if (status_.is_stable && status_.stable_weight_g > CUP_PRESENCE_MIN_G) {
            float return_w = status_.stable_weight_g;
            float delta = status_.baseline_weight_g - return_w;
            CupState old_st = status_.cup_state;
            status_.cup_state = CupState::CUP_PRESENT;
            emit_event(DomainEventType::STATE_CHANGE, now_ms, old_st, status_.cup_state, return_w);

            if (delta >= DRINK_MIN_DELTA_G) {
                // Valid drink: >= 15.0g delta
                status_.consumed_today_ml += delta;
                status_.drink_count++;
                status_.baseline_weight_g = return_w;
                status_.alert_state = AlertState::COUNTING;
                last_drink_time_ms_ = now_ms;
                update_progress_pct();
                emit_event(DomainEventType::DRINK_LOGGED, now_ms, old_st, status_.cup_state, delta, status_.consumed_today_ml);

                if (status_.consumed_today_ml >= status_.daily_goal_ml && !goal_notified_) {
                    goal_notified_ = true;
                    emit_event(DomainEventType::GOAL_REACHED, now_ms, old_st, status_.cup_state, status_.daily_goal_ml, status_.consumed_today_ml);
                }
            } else if ((return_w - status_.baseline_weight_g) >= REFILL_MIN_DELTA_G) {
                // Refill: >= 30.0g increase
                float refill_gain = return_w - status_.baseline_weight_g;
                status_.baseline_weight_g = return_w;
                emit_event(DomainEventType::REFILL_DETECTED, now_ms, old_st, status_.cup_state, refill_gain, return_w);
            } else {
                // Lift without drink (< 15.0g reduction and < 30.0g gain)
                status_.baseline_weight_g = return_w;
                emit_event(DomainEventType::LIFT_NO_DRINK, now_ms, old_st, status_.cup_state, delta, return_w);
            }
        }
    }
}

void CoasterDomain::tick_reminder_timers(uint32_t now_ms) {
    if (!session_active_ || status_.cup_state == CupState::EMPTY) {
        status_.alert_state = AlertState::IDLE;
        status_.countdown_s = get_reminder_interval_ms() / 1000;
        status_.reminder_remaining_ms = get_reminder_interval_ms();
        status_.alert_active = false;
        return;
    }

    if (status_.alert_state == AlertState::COUNTING) {
        uint32_t elapsed = now_ms - last_drink_time_ms_;
        uint32_t interval = get_reminder_interval_ms();
        if (elapsed >= interval) {
            status_.alert_state = AlertState::ALERT_DUE;
            last_alert_time_ms_ = now_ms;
            alert_count_ = 0;
            status_.alert_active = true;
            status_.countdown_s = 0;
            status_.reminder_remaining_ms = 0;
            emit_event(DomainEventType::REMINDER_ALERT, now_ms, status_.cup_state, status_.cup_state, 0.0f, 0.0f);
        } else {
            status_.reminder_remaining_ms = interval - elapsed;
            status_.countdown_s = status_.reminder_remaining_ms / 1000;
            status_.alert_active = false;
        }
    } else if (status_.alert_state == AlertState::ALERT_DUE || status_.alert_state == AlertState::ALERT_REPEAT) {
        status_.alert_active = true;
        status_.countdown_s = 0;
        status_.reminder_remaining_ms = 0;

        uint32_t elapsed = now_ms - last_alert_time_ms_;
        uint32_t repeat_interval = get_repeat_interval_ms();
        if (elapsed >= repeat_interval) {
            status_.alert_state = AlertState::ALERT_REPEAT;
            last_alert_time_ms_ = now_ms;
            alert_count_++;
            emit_event(DomainEventType::REPEAT_ALERT, now_ms, status_.cup_state, status_.cup_state, static_cast<float>(alert_count_), 0.0f);
        }
    }
}

void CoasterDomain::update_progress_pct() {
    if (status_.daily_goal_ml > 0.0f) {
        status_.progress_pct = (status_.consumed_today_ml / status_.daily_goal_ml) * 100.0f;
    } else {
        status_.progress_pct = 0.0f;
    }
}

void CoasterDomain::manual_tare() {
    status_.tare_offset_g += status_.net_weight_g;
    status_.net_weight_g = 0.0f;
    status_.stable_weight_g = 0.0f;
    auto_tare_active_ = false;
    clear_stability_buffer();
}

void CoasterDomain::reset() {
    status_.consumed_today_ml = 0.0f;
    status_.drink_count = 0;
    goal_notified_ = false;
    update_progress_pct();
    last_drink_time_ms_ = current_time_ms_;
    last_alert_time_ms_ = 0;
    alert_count_ = 0;
    status_.countdown_s = get_reminder_interval_ms() / 1000;
    status_.reminder_remaining_ms = get_reminder_interval_ms();

    tap_active_ = false;
    tap_count_ = 0;
    wifi_ap_trigger_pending_ = false;
    for (size_t i = 0; i < TAP_REQUIRED_COUNT; ++i) {
        tap_times_[i] = 0;
    }

    if (status_.net_weight_g < CUP_LIFT_MAX_G) {
        status_.cup_state = CupState::EMPTY;
        status_.alert_state = AlertState::IDLE;
        status_.alert_active = false;
        session_active_ = false;
        clear_stability_buffer();
        auto_tare_active_ = false;
    } else {
        status_.alert_state = AlertState::COUNTING;
        status_.alert_active = false;
        session_active_ = true;
    }
}

void CoasterDomain::set_daily_goal(float goal_ml) {
    if (goal_ml >= 100.0f && goal_ml <= 10000.0f) {
        status_.daily_goal_ml = goal_ml;
        update_progress_pct();
    }
}

void CoasterDomain::set_demo_mode(bool enable) {
    demo_mode_ = enable;
    status_.demo_mode = enable;
    if (!session_active_ || status_.cup_state == CupState::EMPTY) {
        status_.countdown_s = get_reminder_interval_ms() / 1000;
        status_.reminder_remaining_ms = get_reminder_interval_ms();
    }
}

bool CoasterDomain::consume_wifi_ap_trigger() {
    bool pending = wifi_ap_trigger_pending_;
    wifi_ap_trigger_pending_ = false;
    return pending;
}

void CoasterDomain::evaluate_tap_gesture(uint32_t now_ms, float net_w) {
    if (status_.cup_state == CupState::CUP_PRESENT ||
        status_.cup_state == CupState::CUP_LIFTED ||
        status_.cup_state == CupState::RETURN_STABILIZING) {
        tap_active_ = false;
        tap_count_ = 0;
        return;
    }

    if (!tap_active_) {
        if (net_w > CUP_PRESENCE_MIN_G) {
            tap_active_ = true;
            tap_start_ms_ = now_ms;
            tap_peak_g_ = net_w;
        }
    } else {
        if (net_w > tap_peak_g_) {
            tap_peak_g_ = net_w;
        }
        if (net_w <= CUP_PRESENCE_MIN_G) {
            uint32_t dur = now_ms - tap_start_ms_;
            tap_active_ = false;
            if (dur >= TAP_MIN_DURATION_MS && dur <= TAP_MAX_DURATION_MS && tap_peak_g_ >= TAP_MIN_WEIGHT_G) {
                size_t valid = 0;
                for (size_t i = 0; i < tap_count_; ++i) {
                    if ((now_ms - tap_times_[i]) <= TAP_WINDOW_MS) {
                        tap_times_[valid++] = tap_times_[i];
                    }
                }
                tap_count_ = valid;
                if (tap_count_ < TAP_REQUIRED_COUNT) {
                    tap_times_[tap_count_++] = now_ms;
                }
                if (tap_count_ >= TAP_REQUIRED_COUNT) {
                    wifi_ap_trigger_pending_ = true;
                    tap_count_ = 0;
                }
            }
        }
    }
}

void CoasterDomain::set_event_callback(EventCallback cb, void* user_data) {
    event_callback_ = cb;
    callback_user_data_ = user_data;
}

DomainEvent CoasterDomain::pop_event() {
    if (event_count_ == 0) {
        return DomainEvent{ DomainEventType::NONE, 0, CupState::EMPTY, CupState::EMPTY, 0.0f, 0.0f };
    }
    DomainEvent ev = event_queue_[event_tail_];
    event_tail_ = (event_tail_ + 1) % EVENT_QUEUE_CAPACITY;
    event_count_--;
    return ev;
}

void CoasterDomain::push_event(const DomainEvent& ev) {
    if (event_count_ < EVENT_QUEUE_CAPACITY) {
        event_queue_[event_head_] = ev;
        event_head_ = (event_head_ + 1) % EVENT_QUEUE_CAPACITY;
        event_count_++;
    } else {
        event_queue_[event_head_] = ev;
        event_head_ = (event_head_ + 1) % EVENT_QUEUE_CAPACITY;
        event_tail_ = (event_tail_ + 1) % EVENT_QUEUE_CAPACITY;
    }
    if (event_callback_) {
        event_callback_(ev, callback_user_data_);
    }
}

void CoasterDomain::emit_event(DomainEventType type, uint32_t now_ms,
                               CupState old_st, CupState new_st,
                               float val1, float val2) {
    DomainEvent ev;
    ev.type = type;
    ev.timestamp_ms = now_ms;
    ev.old_cup_state = old_st;
    ev.new_cup_state = new_st;
    ev.value1 = val1;
    ev.value2 = val2;
    push_event(ev);
}

const char* CoasterDomain::cup_state_to_string(CupState state) {
    switch (state) {
        case CupState::EMPTY: return "EMPTY";
        case CupState::STABILIZING: return "STABILIZING";
        case CupState::CUP_PRESENT: return "CUP_PRESENT";
        case CupState::CUP_LIFTED: return "CUP_LIFTED";
        case CupState::RETURN_STABILIZING: return "RETURN_STABILIZING";
        default: return "UNKNOWN";
    }
}

const char* CoasterDomain::alert_state_to_string(AlertState state) {
    switch (state) {
        case AlertState::IDLE: return "IDLE";
        case AlertState::COUNTING: return "COUNTING";
        case AlertState::ALERT_DUE: return "ALERT_DUE";
        case AlertState::ALERT_REPEAT: return "ALERT_REPEAT";
        default: return "UNKNOWN";
    }
}

const char* CoasterDomain::event_type_to_string(DomainEventType type) {
    switch (type) {
        case DomainEventType::NONE: return "NONE";
        case DomainEventType::STATE_CHANGE: return "STATE_CHANGE";
        case DomainEventType::CUP_PLACED: return "CUP_PLACED";
        case DomainEventType::CUP_LIFTED: return "CUP_LIFTED";
        case DomainEventType::DRINK_LOGGED: return "DRINK_LOGGED";
        case DomainEventType::LIFT_NO_DRINK: return "LIFT_NO_DRINK";
        case DomainEventType::REFILL_DETECTED: return "REFILL_DETECTED";
        case DomainEventType::REMINDER_ALERT: return "REMINDER_ALERT";
        case DomainEventType::REPEAT_ALERT: return "REPEAT_ALERT";
        case DomainEventType::AUTO_TARE_APPLIED: return "AUTO_TARE";
        case DomainEventType::GOAL_REACHED: return "GOAL_REACHED";
        default: return "UNKNOWN";
    }
}
