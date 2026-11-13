#ifndef COASTER_DOMAIN_H
#define COASTER_DOMAIN_H

#include <cstdint>
#include <cstddef>

/**
 * @brief State of cup placement on the coaster.
 */
enum class CupState : uint8_t {
    EMPTY = 0,             // No cup on coaster (< 20g net)
    STABILIZING,           // Placed weight > 30g, waiting for stability filter
    CUP_PRESENT,           // Cup resting stably on coaster, baseline locked
    CUP_LIFTED,            // Cup removed (< 20g net), user drinking or refilling
    RETURN_STABILIZING     // Cup returned (> 30g net), settling before delta evaluation
};

/**
 * @brief State of the hydration reminder alert system.
 */
enum class AlertState : uint8_t {
    IDLE = 0,              // Coaster empty, no active hydration session
    COUNTING,              // Cup placed, counting down from reminder interval (45m / 45s)
    ALERT_DUE,             // Inactivity limit reached, reminder pulse active
    ALERT_REPEAT           // Inactivity persists, repeating reminder active (5m / 5s)
};

/**
 * @brief Domain event types emitted on state transitions and telemetry triggers.
 */
enum class DomainEventType : uint8_t {
    NONE = 0,
    STATE_CHANGE,
    CUP_PLACED,
    CUP_LIFTED,
    DRINK_LOGGED,
    LIFT_NO_DRINK,
    REFILL_DETECTED,
    REMINDER_ALERT,
    REPEAT_ALERT,
    AUTO_TARE_APPLIED,
    GOAL_REACHED
};

/**
 * @brief Structured event descriptor emitted by CoasterDomain.
 */
struct DomainEvent {
    DomainEventType type;
    uint32_t timestamp_ms;
    CupState old_cup_state;
    CupState new_cup_state;
    float value1; // drink delta, refill delta, tare offset, or daily goal
    float value2; // consumed total, baseline weight, or alert count
};

/**
 * @brief Snapshot of coaster operational metrics and state.
 */
struct CoasterStatus {
    CupState cup_state;
    AlertState alert_state;
    float raw_weight_g;
    float tare_offset_g;
    float net_weight_g;
    float stable_weight_g;
    float baseline_weight_g;
    float consumed_today_ml;
    float daily_goal_ml;
    float progress_pct;
    uint32_t drink_count;
    uint32_t reminder_remaining_ms;
    uint32_t countdown_s;
    bool is_stable;
    bool alert_active;
    bool demo_mode;
};

/**
 * @brief Pure C++ domain logic state machine for smart hydration coaster.
 * Decoupled from hardware drivers and communications (zero <Arduino.h> dependency).
 */
class CoasterDomain {
public:
    // Domain Threshold Constants
    static constexpr float AUTO_TARE_WINDOW_G          = 5.0f;
    static constexpr float AUTO_TARE_MIN_DRIFT_G       = 0.05f;
    static constexpr uint32_t AUTO_TARE_TIME_MS_NORMAL = 5000;
    static constexpr uint32_t AUTO_TARE_TIME_MS_DEMO   = 1000;

    static constexpr float CUP_PRESENCE_MIN_G          = 30.0f;
    static constexpr float CUP_LIFT_MAX_G              = 20.0f;

    static constexpr float STABILITY_MAX_SPAN_G        = 3.0f;
    static constexpr uint32_t STABILITY_WINDOW_MS       = 1500;
    static constexpr size_t STABILITY_MIN_SAMPLES      = 3;

    static constexpr float DRINK_MIN_DELTA_G           = 15.0f;
    static constexpr float REFILL_MIN_DELTA_G          = 30.0f;

    static constexpr uint32_t REMINDER_INTERVAL_MS_NORMAL = 45UL * 60UL * 1000UL;
    static constexpr uint32_t REMINDER_INTERVAL_MS_DEMO   = 45UL * 1000UL;

    static constexpr uint32_t REPEAT_INTERVAL_MS_NORMAL   = 5UL * 60UL * 1000UL;
    static constexpr uint32_t REPEAT_INTERVAL_MS_DEMO     = 5UL * 1000UL;

    static constexpr size_t EVENT_QUEUE_CAPACITY       = 16;
    static constexpr size_t STABILITY_BUFFER_CAPACITY   = 128; // Up to 80Hz sampling support

    static constexpr float TAP_MIN_WEIGHT_G            = 60.0f;
    static constexpr uint32_t TAP_MIN_DURATION_MS       = 40UL;
    static constexpr uint32_t TAP_MAX_DURATION_MS       = 600UL;
    static constexpr uint32_t TAP_WINDOW_MS             = 4000UL;
    static constexpr size_t TAP_REQUIRED_COUNT         = 3;

    typedef void (*EventCallback)(const DomainEvent& event, void* user_data);

    explicit CoasterDomain(float daily_goal_ml = 2000.0f);

    void update(float raw_weight_g, uint32_t now_ms);

    void manual_tare();
    void reset();
    void set_daily_goal(float goal_ml);
    void set_demo_mode(bool enable);
    bool consume_wifi_ap_trigger();

    const CoasterStatus& get_status() const { return status_; }
    bool is_demo_mode() const { return demo_mode_; }
    float get_progress_pct() const { return status_.progress_pct; }

    bool has_events() const { return event_count_ > 0; }
    DomainEvent pop_event();

    void set_event_callback(EventCallback cb, void* user_data = nullptr);

    static const char* cup_state_to_string(CupState state);
    static const char* alert_state_to_string(AlertState state);
    static const char* event_type_to_string(DomainEventType type);

private:
    CoasterStatus status_{};
    bool demo_mode_ = false;
    bool session_active_ = false;
    bool goal_notified_ = false;

    uint32_t current_time_ms_ = 0;
    uint32_t last_drink_time_ms_ = 0;
    uint32_t last_alert_time_ms_ = 0;
    uint32_t alert_count_ = 0;

    uint32_t auto_tare_start_ms_ = 0;
    bool auto_tare_active_ = false;

    bool tap_active_ = false;
    uint32_t tap_start_ms_ = 0;
    float tap_peak_g_ = 0.0f;
    uint32_t tap_times_[TAP_REQUIRED_COUNT] = {0};
    size_t tap_count_ = 0;
    bool wifi_ap_trigger_pending_ = false;

    struct Sample {
        uint32_t time_ms;
        float weight_g;
    };
    Sample stability_buffer_[STABILITY_BUFFER_CAPACITY];
    size_t stability_count_ = 0;
    size_t stability_head_ = 0;
    size_t stability_tail_ = 0;

    DomainEvent event_queue_[EVENT_QUEUE_CAPACITY];
    size_t event_head_ = 0;
    size_t event_tail_ = 0;
    size_t event_count_ = 0;

    EventCallback event_callback_ = nullptr;
    void* callback_user_data_ = nullptr;

    void record_stability_sample(uint32_t now_ms, float net_weight);
    void evaluate_stability(uint32_t now_ms);
    void clear_stability_buffer();

    void tick_state_machine(uint32_t now_ms);
    void tick_reminder_timers(uint32_t now_ms);
    void evaluate_tap_gesture(uint32_t now_ms, float net_w);
    void update_progress_pct();

    void push_event(const DomainEvent& ev);
    void emit_event(DomainEventType type, uint32_t now_ms,
                    CupState old_st, CupState new_st,
                    float val1 = 0.0f, float val2 = 0.0f);

    uint32_t get_reminder_interval_ms() const {
        return demo_mode_ ? REMINDER_INTERVAL_MS_DEMO : REMINDER_INTERVAL_MS_NORMAL;
    }
    uint32_t get_repeat_interval_ms() const {
        return demo_mode_ ? REPEAT_INTERVAL_MS_DEMO : REPEAT_INTERVAL_MS_NORMAL;
    }
    uint32_t get_auto_tare_time_ms() const {
        return demo_mode_ ? AUTO_TARE_TIME_MS_DEMO : AUTO_TARE_TIME_MS_NORMAL;
    }
};

#endif // COASTER_DOMAIN_H
