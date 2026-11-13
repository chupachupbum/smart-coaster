/**
 * @file network_push.cpp
 * @brief Non-blocking Wi-Fi manager and ntfy.sh HTTP push notification client implementation.
 */

#include "network_push.h"
#include <cstdio>
#include <cstring>
#include <cmath>

NetworkPush::NetworkPush()
    : wifi_state_(WifiState::DISCONNECTED),
      server_reachability_(ServerReachability::UNKNOWN),
      last_error_(WifiErrorReason::NONE),
      consecutive_sta_failures_(0),
      ap_active_(false),
      ap_shutdown_pending_(false),
      switch_pending_(false),
      switching_in_progress_(false),
      rollback_occurred_(false),
      wifi_connected_trigger_(false),
      wifi_error_trigger_(false),
      state_entry_ms_(0),
      retry_backoff_ms_(WIFI_RETRY_INTERVAL_MS),
      switch_armed_ms_(0),
      ap_shutdown_armed_ms_(0),
      last_ap_bg_retry_ms_(0),
      queue_head_(0),
      queue_count_(0),
      has_posted_(false),
      last_post_ms_(0),
      retry_wait_start_ms_(0),
      retry_delay_ms_(0),
      post_success_count_(0),
      post_fail_count_(0),
      dropped_count_(0),
      exhausted_retry_count_(0),
      last_http_code_(0),
      scanning_(false),
      scan_start_ms_(0),
      last_scan_ms_(0),
      scan_count_(0),
      host_auto_connect_(true),
      simulated_connect_ok_(true),
      simulated_fail_reason_(WifiErrorReason::AUTH_FAILED),
      simulated_http_success_(true),
      simulated_http_code_(200),
      simulated_ap_clients_(0) {
    ssid_[0] = '\0';
    password_[0] = '\0';
    prev_ssid_[0] = '\0';
    prev_password_[0] = '\0';
    pending_ssid_[0] = '\0';
    pending_pass_[0] = '\0';
    failed_ssid_[0] = '\0';
    topic_[0] = '\0';
    server_url_[0] = '\0';
    simulated_good_ssid_[0] = '\0';
    memset(queue_, 0, sizeof(queue_));
    memset(scan_results_, 0, sizeof(scan_results_));
}

void NetworkPush::begin(const char* ssid, const char* password, const char* ntfy_topic, const char* server_url) {
    setCredentials(ssid, password);
    setTopic(ntfy_topic);
    setServerUrl(server_url);

    retry_backoff_ms_ = WIFI_RETRY_INTERVAL_MS;
    queue_head_ = 0;
    queue_count_ = 0;
    has_posted_ = false;
    last_post_ms_ = 0;
    retry_wait_start_ms_ = 0;
    retry_delay_ms_ = 0;
    consecutive_sta_failures_ = 0;
    ap_active_ = false;
    ap_shutdown_pending_ = false;
    switch_pending_ = false;
    switching_in_progress_ = false;
    rollback_occurred_ = false;
    wifi_connected_trigger_ = false;
    wifi_error_trigger_ = false;
    last_error_ = WifiErrorReason::NONE;
    failed_ssid_[0] = '\0';
    prev_ssid_[0] = '\0';
    prev_password_[0] = '\0';
    server_reachability_ = ServerReachability::UNKNOWN;

#ifdef ARDUINO
    Preferences prefs;
    if (prefs.begin("coaster_net", true)) {
        String nvs_ssid = prefs.getString("ssid", "");
        String nvs_pass = prefs.getString("pass", "");
        String nvs_prev_ssid = prefs.getString("prev_ssid", "");
        String nvs_prev_pass = prefs.getString("prev_pass", "");
        String nvs_topic = prefs.getString("topic", "");
        String nvs_srv = prefs.getString("srv_url", "");
        prefs.end();

        if (nvs_ssid.length() > 0) {
            strncpy(ssid_, nvs_ssid.c_str(), sizeof(ssid_) - 1);
            ssid_[sizeof(ssid_) - 1] = '\0';
            strncpy(password_, nvs_pass.c_str(), sizeof(password_) - 1);
            password_[sizeof(password_) - 1] = '\0';
        }
        if (nvs_prev_ssid.length() > 0) {
            strncpy(prev_ssid_, nvs_prev_ssid.c_str(), sizeof(prev_ssid_) - 1);
            prev_ssid_[sizeof(prev_ssid_) - 1] = '\0';
            strncpy(prev_password_, nvs_prev_pass.c_str(), sizeof(prev_password_) - 1);
            prev_password_[sizeof(prev_password_) - 1] = '\0';
        }
        if (nvs_topic.length() > 0) {
            strncpy(topic_, nvs_topic.c_str(), sizeof(topic_) - 1);
            topic_[sizeof(topic_) - 1] = '\0';
        }
        if (nvs_srv.length() > 0) {
            strncpy(server_url_, nvs_srv.c_str(), sizeof(server_url_) - 1);
            server_url_[sizeof(server_url_) - 1] = '\0';
        }
    }

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false);
#endif

    if (strlen(ssid_) == 0) {
        startApFallback(0);
    } else {
        reconnect(0);
    }
}

void NetworkPush::setCredentials(const char* ssid, const char* password) {
    if (ssid) {
        strncpy(ssid_, ssid, sizeof(ssid_) - 1);
        ssid_[sizeof(ssid_) - 1] = '\0';
    } else {
        ssid_[0] = '\0';
    }
    if (password) {
        strncpy(password_, password, sizeof(password_) - 1);
        password_[sizeof(password_) - 1] = '\0';
    } else {
        password_[0] = '\0';
    }
}

void NetworkPush::setTopic(const char* ntfy_topic) {
    if (ntfy_topic && strlen(ntfy_topic) > 0) {
        strncpy(topic_, ntfy_topic, sizeof(topic_) - 1);
        topic_[sizeof(topic_) - 1] = '\0';
    } else {
        strncpy(topic_, NTFY_DEFAULT_TOPIC, sizeof(topic_) - 1);
        topic_[sizeof(topic_) - 1] = '\0';
    }
}

void NetworkPush::setServerUrl(const char* server_url) {
    if (server_url && strlen(server_url) > 0) {
        strncpy(server_url_, server_url, sizeof(server_url_) - 1);
        server_url_[sizeof(server_url_) - 1] = '\0';
    } else {
        strncpy(server_url_, NTFY_SERVER_URL, sizeof(server_url_) - 1);
        server_url_[sizeof(server_url_) - 1] = '\0';
    }
}

void NetworkPush::disconnect() {
#ifdef ARDUINO
    WiFi.disconnect(false);
#endif
    wifi_state_ = WifiState::DISCONNECTED;
    state_entry_ms_ = 0;
}

void NetworkPush::startStaConnect(uint32_t now_ms) {
    if (strlen(ssid_) == 0) {
        wifi_state_ = WifiState::DISCONNECTED;
        return;
    }

#ifdef ARDUINO
    WiFi.disconnect(false);
    if (ap_active_) {
        WiFi.mode(WIFI_AP_STA);
    } else {
        WiFi.mode(WIFI_STA);
    }
    WiFi.begin(ssid_, strlen(password_) > 0 ? password_ : nullptr);
    state_entry_ms_ = millis();
#else
    state_entry_ms_ = now_ms;
#endif
    wifi_state_ = WifiState::CONNECTING;
}

void NetworkPush::reconnect(uint32_t now_ms) {
    startStaConnect(now_ms);
}

void NetworkPush::startApFallback(uint32_t now_ms) {
    wifi_state_ = WifiState::AP_FALLBACK;
    ap_active_ = true;
    ap_shutdown_pending_ = false;
    last_ap_bg_retry_ms_ = now_ms;
    wifi_error_trigger_ = true;

#ifdef ARDUINO
    WiFi.disconnect(false);
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(WIFI_AP_SSID);
#endif

    startAsyncScan(now_ms);
}

void NetworkPush::update(uint32_t now_ms) {
    tickWifi(now_ms);
    pollAsyncScan(now_ms);
    processQueue(now_ms);
}

void NetworkPush::tickWifi(uint32_t now_ms) {
    // 1. Check deferred Wi-Fi network switch
    if (switch_pending_ && (static_cast<uint32_t>(now_ms - switch_armed_ms_) >= WIFI_SWITCH_DEFER_MS)) {
        switch_pending_ = false;
        strncpy(ssid_, pending_ssid_, sizeof(ssid_) - 1);
        ssid_[sizeof(ssid_) - 1] = '\0';
        strncpy(password_, pending_pass_, sizeof(password_) - 1);
        password_[sizeof(password_) - 1] = '\0';
        switching_in_progress_ = true;
        consecutive_sta_failures_ = 0;
        startStaConnect(now_ms);
    }

    // 2. Check pending SoftAP shutdown after grace period
    if (ap_shutdown_pending_ && (static_cast<uint32_t>(now_ms - ap_shutdown_armed_ms_) >= WIFI_AP_SHUTDOWN_GRACE_MS)) {
#ifdef ARDUINO
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);
#endif
        ap_active_ = false;
        ap_shutdown_pending_ = false;
    }

    // 3. State machine evaluation
    switch (wifi_state_) {
        case WifiState::DISCONNECTED: {
            if (strlen(ssid_) > 0 && (static_cast<uint32_t>(now_ms - state_entry_ms_) >= retry_backoff_ms_)) {
                startStaConnect(now_ms);
            }
            break;
        }

        case WifiState::CONNECTING: {
            bool connect_success = false;
            bool connect_failure = false;
            WifiErrorReason reason = WifiErrorReason::NONE;

#ifdef ARDUINO
            wl_status_t status = WiFi.status();
            if (status == WL_CONNECTED) {
                connect_success = true;
            } else if (status == WL_CONNECT_FAILED) {
                connect_failure = true;
                reason = WifiErrorReason::AUTH_FAILED;
            } else if (status == WL_NO_SSID_AVAIL) {
                connect_failure = true;
                reason = WifiErrorReason::SSID_NOT_FOUND;
            } else if (static_cast<uint32_t>(now_ms - state_entry_ms_) >= WIFI_CONNECT_TIMEOUT_MS) {
                connect_failure = true;
                reason = WifiErrorReason::TIMEOUT;
            }
#else
            if (host_auto_connect_) {
                connect_success = true;
            } else {
                bool should_succeed = simulated_connect_ok_;
                if (strlen(simulated_good_ssid_) > 0) {
                    should_succeed = (strcmp(ssid_, simulated_good_ssid_) == 0);
                }
                if (should_succeed) {
                    connect_success = true;
                } else {
                    reason = simulated_fail_reason_;
                    if (reason == WifiErrorReason::TIMEOUT) {
                        if (static_cast<uint32_t>(now_ms - state_entry_ms_) >= WIFI_CONNECT_TIMEOUT_MS) {
                            connect_failure = true;
                        }
                    } else {
                        connect_failure = true;
                    }
                }
            }
#endif

            if (connect_success) {
                wifi_state_ = WifiState::CONNECTED;
                state_entry_ms_ = now_ms;
                consecutive_sta_failures_ = 0;
                retry_backoff_ms_ = WIFI_FAST_RETRY_MS;
                wifi_connected_trigger_ = true;
                if (server_reachability_ == ServerReachability::UNKNOWN) {
                    server_reachability_ = ServerReachability::REACHABLE;
                }
                if (switching_in_progress_) {
                    switching_in_progress_ = false;
                    rollback_occurred_ = false;
                    saveConfigToNvs();
                }
                if (ap_active_) {
                    ap_shutdown_pending_ = true;
                    ap_shutdown_armed_ms_ = now_ms;
                }
#ifdef ARDUINO
                Serial.print(F("[NET] Wi-Fi Connected! IP: "));
                Serial.println(WiFi.localIP());
#endif
            } else if (connect_failure) {
#ifdef ARDUINO
                WiFi.disconnect(false);
#endif
                last_error_ = reason;

                // Check safe switch rollback condition
                if (switching_in_progress_ && strlen(prev_ssid_) > 0) {
                    strncpy(failed_ssid_, ssid_, sizeof(failed_ssid_) - 1);
                    failed_ssid_[sizeof(failed_ssid_) - 1] = '\0';
                    strncpy(ssid_, prev_ssid_, sizeof(ssid_) - 1);
                    ssid_[sizeof(ssid_) - 1] = '\0';
                    strncpy(password_, prev_password_, sizeof(password_) - 1);
                    password_[sizeof(password_) - 1] = '\0';
                    switching_in_progress_ = false;
                    rollback_occurred_ = true;
                    wifi_error_trigger_ = true;
                    consecutive_sta_failures_ = 0;
                    startStaConnect(now_ms);
                } else {
                    consecutive_sta_failures_++;
                    if (consecutive_sta_failures_ >= WIFI_AP_FALLBACK_FAILURES) {
                        startApFallback(now_ms);
                    } else {
                        wifi_state_ = WifiState::CONNECT_FAILED;
                        state_entry_ms_ = now_ms;
                        retry_backoff_ms_ = WIFI_FAST_RETRY_MS;
                    }
                }
            }
            break;
        }

        case WifiState::CONNECTED: {
#ifdef ARDUINO
            if (WiFi.status() != WL_CONNECTED) {
                wifi_state_ = WifiState::DISCONNECTED;
                state_entry_ms_ = now_ms;
                retry_backoff_ms_ = WIFI_FAST_RETRY_MS;
                Serial.println(F("[NET] Wi-Fi Connection Lost."));
            }
#endif
            break;
        }

        case WifiState::CONNECT_FAILED: {
            if (static_cast<uint32_t>(now_ms - state_entry_ms_) >= retry_backoff_ms_) {
                startStaConnect(now_ms);
            }
            break;
        }

        case WifiState::AP_FALLBACK: {
            if (getApClientCount() > 0) {
                last_ap_bg_retry_ms_ = now_ms;
            } else if (strlen(ssid_) > 0 &&
                       (static_cast<uint32_t>(now_ms - last_ap_bg_retry_ms_) >= WIFI_AP_BG_RETRY_MS)) {
                last_ap_bg_retry_ms_ = now_ms;
                startStaConnect(now_ms);
            }
            break;
        }
    }
}

bool NetworkPush::saveConfigToNvs() {
#ifdef ARDUINO
    Preferences prefs;
    if (!prefs.begin("coaster_net", false)) {
        return false;
    }
    prefs.putString("ssid", ssid_);
    prefs.putString("pass", password_);
    prefs.putString("prev_ssid", prev_ssid_);
    prefs.putString("prev_pass", prev_password_);
    prefs.putString("topic", topic_);
    prefs.putString("srv_url", server_url_);
    prefs.end();
    return true;
#else
    return true;
#endif
}

void NetworkPush::forgetCredentials(uint32_t now_ms) {
    ssid_[0] = '\0';
    password_[0] = '\0';
    prev_ssid_[0] = '\0';
    prev_password_[0] = '\0';
    pending_ssid_[0] = '\0';
    pending_pass_[0] = '\0';
    failed_ssid_[0] = '\0';
    switch_pending_ = false;
    switching_in_progress_ = false;
    rollback_occurred_ = false;
    consecutive_sta_failures_ = 0;

#ifdef ARDUINO
    Preferences prefs;
    if (prefs.begin("coaster_net", false)) {
        prefs.remove("ssid");
        prefs.remove("pass");
        prefs.remove("prev_ssid");
        prefs.remove("prev_pass");
        prefs.end();
    }
#endif

    startApFallback(now_ms);
}

bool NetworkPush::requestWifiSwitch(const char* new_ssid, const char* new_pass, uint32_t now_ms) {
    if (!new_ssid || strlen(new_ssid) == 0 || strlen(new_ssid) > 32) {
        return false;
    }

    if (strlen(ssid_) > 0 && !switching_in_progress_) {
        strncpy(prev_ssid_, ssid_, sizeof(prev_ssid_) - 1);
        prev_ssid_[sizeof(prev_ssid_) - 1] = '\0';
        strncpy(prev_password_, password_, sizeof(prev_password_) - 1);
        prev_password_[sizeof(prev_password_) - 1] = '\0';
    }

    strncpy(pending_ssid_, new_ssid, sizeof(pending_ssid_) - 1);
    pending_ssid_[sizeof(pending_ssid_) - 1] = '\0';

    if (new_pass) {
        strncpy(pending_pass_, new_pass, sizeof(pending_pass_) - 1);
        pending_pass_[sizeof(pending_pass_) - 1] = '\0';
    } else {
        pending_pass_[0] = '\0';
    }

    switch_pending_ = true;
    switch_armed_ms_ = now_ms;
    rollback_occurred_ = false;
    last_error_ = WifiErrorReason::NONE;
    failed_ssid_[0] = '\0';
    return true;
}

void NetworkPush::getIpString(char* out, size_t len) const {
    if (!out || len == 0) return;
#ifdef ARDUINO
    if (wifi_state_ == WifiState::CONNECTED) {
        String ip = WiFi.localIP().toString();
        strncpy(out, ip.c_str(), len - 1);
        out[len - 1] = '\0';
    } else if (ap_active_) {
        String ip = WiFi.softAPIP().toString();
        strncpy(out, ip.c_str(), len - 1);
        out[len - 1] = '\0';
    } else {
        strncpy(out, "0.0.0.0", len - 1);
        out[len - 1] = '\0';
    }
#else
    if (wifi_state_ == WifiState::CONNECTED) {
        strncpy(out, "192.168.1.100", len - 1);
    } else if (ap_active_) {
        strncpy(out, "192.168.4.1", len - 1);
    } else {
        strncpy(out, "0.0.0.0", len - 1);
    }
    out[len - 1] = '\0';
#endif
}

int32_t NetworkPush::getRssi() const {
#ifdef ARDUINO
    return (wifi_state_ == WifiState::CONNECTED) ? WiFi.RSSI() : 0;
#else
    return (wifi_state_ == WifiState::CONNECTED) ? -65 : 0;
#endif
}

void NetworkPush::getMacString(char* out, size_t len) const {
    if (!out || len == 0) return;
#ifdef ARDUINO
    String mac = WiFi.macAddress();
    strncpy(out, mac.c_str(), len - 1);
    out[len - 1] = '\0';
#else
    strncpy(out, "AA:BB:CC:DD:EE:FF", len - 1);
    out[len - 1] = '\0';
#endif
}

uint8_t NetworkPush::getApClientCount() const {
#ifdef ARDUINO
    return ap_active_ ? WiFi.softAPgetStationNum() : 0;
#else
    return simulated_ap_clients_;
#endif
}

bool NetworkPush::consumeWifiConnectedTrigger() {
    bool val = wifi_connected_trigger_;
    wifi_connected_trigger_ = false;
    return val;
}

bool NetworkPush::consumeWifiErrorTrigger() {
    bool val = wifi_error_trigger_;
    wifi_error_trigger_ = false;
    return val;
}

bool NetworkPush::startAsyncScan(uint32_t now_ms) {
    if (scanning_) return false;
    scanning_ = true;
    scan_start_ms_ = now_ms;
#ifdef ARDUINO
    WiFi.scanNetworks(true, true);
#endif
    return true;
}

void NetworkPush::pollAsyncScan(uint32_t now_ms) {
    if (!scanning_) return;

#ifdef ARDUINO
    int16_t scan_res = WiFi.scanComplete();
    if (scan_res == WIFI_SCAN_RUNNING) {
        return;
    }
    if (scan_res >= 0) {
        WifiScanEntry raw[32];
        size_t count = (scan_res > 32) ? 32 : static_cast<size_t>(scan_res);
        for (size_t i = 0; i < count; ++i) {
            String s = WiFi.SSID(i);
            strncpy(raw[i].ssid, s.c_str(), sizeof(raw[i].ssid) - 1);
            raw[i].ssid[sizeof(raw[i].ssid) - 1] = '\0';
            raw[i].rssi = WiFi.RSSI(i);
            raw[i].channel = WiFi.channel(i);
            raw[i].auth_open = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
        }
        ingestScanEntries(raw, count);
        WiFi.scanDelete();
    }
    scanning_ = false;
    last_scan_ms_ = now_ms;
#else
    scanning_ = false;
    last_scan_ms_ = now_ms;
#endif
}

void NetworkPush::ingestScanEntries(const WifiScanEntry* raw, size_t count) {
    if (!raw || count == 0) {
        scan_count_ = 0;
        return;
    }

    WifiScanEntry temp[64];
    size_t temp_count = 0;

    for (size_t i = 0; i < count; ++i) {
        if (raw[i].ssid[0] == '\0') continue;

        int existing_idx = -1;
        for (size_t j = 0; j < temp_count; ++j) {
            if (strncmp(temp[j].ssid, raw[i].ssid, sizeof(temp[j].ssid)) == 0) {
                existing_idx = static_cast<int>(j);
                break;
            }
        }

        if (existing_idx >= 0) {
            if (raw[i].rssi > temp[existing_idx].rssi) {
                temp[existing_idx] = raw[i];
            }
        } else if (temp_count < sizeof(temp) / sizeof(temp[0])) {
            temp[temp_count++] = raw[i];
        }
    }

    for (size_t i = 0; i < temp_count; ++i) {
        for (size_t j = i + 1; j < temp_count; ++j) {
            if (temp[j].rssi > temp[i].rssi) {
                WifiScanEntry tmp = temp[i];
                temp[i] = temp[j];
                temp[j] = tmp;
            }
        }
    }

    scan_count_ = (temp_count > MAX_SCAN_RESULTS) ? MAX_SCAN_RESULTS : temp_count;
    for (size_t i = 0; i < scan_count_; ++i) {
        scan_results_[i] = temp[i];
    }
}

void NetworkPush::setSimulatedWifiOutcome(bool should_connect, WifiErrorReason fail_reason) {
    simulated_connect_ok_ = should_connect;
    simulated_fail_reason_ = fail_reason;
}

void NetworkPush::setSimulatedSsidFilter(const char* good_ssid) {
    if (good_ssid) {
        strncpy(simulated_good_ssid_, good_ssid, sizeof(simulated_good_ssid_) - 1);
        simulated_good_ssid_[sizeof(simulated_good_ssid_) - 1] = '\0';
    } else {
        simulated_good_ssid_[0] = '\0';
    }
}

void NetworkPush::setSimulatedHttpSuccess(bool ok, int http_code) {
    simulated_http_success_ = ok;
    simulated_http_code_ = http_code;
}

void NetworkPush::setSimulatedScanResults(const WifiScanEntry* entries, size_t count) {
    ingestScanEntries(entries, count);
}

bool NetworkPush::queueNotification(const char* title, const char* message, int priority, const char* tags) {
    if (!title || !message || strlen(title) == 0 || strlen(message) == 0) {
        return false;
    }

    if (queue_count_ >= QUEUE_CAPACITY) {
        queue_head_ = (queue_head_ + 1) % QUEUE_CAPACITY;
        queue_count_--;
        dropped_count_++;
        if (queue_[queue_head_].retry_count == 0) {
            retry_delay_ms_ = 0;
        }
#ifdef ARDUINO
        Serial.println(F("[NET] Queue overflow: dropped oldest notification."));
#endif
    }

    size_t tail = (queue_head_ + queue_count_) % QUEUE_CAPACITY;
    PushNotification& notif = queue_[tail];

    strncpy(notif.title, title, sizeof(notif.title) - 1);
    notif.title[sizeof(notif.title) - 1] = '\0';

    strncpy(notif.message, message, sizeof(notif.message) - 1);
    notif.message[sizeof(notif.message) - 1] = '\0';

    notif.priority = (priority < 1) ? 1 : ((priority > 5) ? 5 : priority);

    if (tags) {
        strncpy(notif.tags, tags, sizeof(notif.tags) - 1);
        notif.tags[sizeof(notif.tags) - 1] = '\0';
    } else {
        notif.tags[0] = '\0';
    }

#ifdef ARDUINO
    notif.timestamp_ms = millis();
#else
    notif.timestamp_ms = 0;
#endif
    notif.retry_count = 0;

    queue_count_++;
    return true;
}

bool NetworkPush::queueDrinkLogged(float drink_delta_ml, float total_consumed_ml, float daily_goal_ml) {
    char msg[128];
    snprintf(msg, sizeof(msg), "Logged %dml! Total today: %d/%dml.",
             (int)roundf(drink_delta_ml),
             (int)roundf(total_consumed_ml),
             (int)roundf(daily_goal_ml));

    return queueNotification(NTFY_TITLE_DRINK_LOGGED, msg, NTFY_PRIORITY_DRINK_LOGGED, NTFY_TAGS_DRINK_LOGGED);
}

bool NetworkPush::queueReminderAlert() {
    return queueNotification(NTFY_TITLE_REMINDER, NTFY_MESSAGE_REMINDER, NTFY_PRIORITY_REMINDER, NTFY_TAGS_REMINDER);
}

bool NetworkPush::queueRepeatAlert() {
    return queueNotification(NTFY_TITLE_REPEAT, NTFY_MESSAGE_REPEAT, NTFY_PRIORITY_REPEAT, NTFY_TAGS_REPEAT);
}

bool NetworkPush::queueGoalAchieved(float daily_goal_ml) {
    char msg[128];
    snprintf(msg, sizeof(msg), "Congratulations! You reached your %dml hydration goal!", (int)roundf(daily_goal_ml));
    return queueNotification(NTFY_TITLE_GOAL_ACHIEVED, msg, NTFY_PRIORITY_GOAL_ACHIEVED, NTFY_TAGS_GOAL_ACHIEVED);
}

void NetworkPush::handleDomainEvent(const DomainEvent& event, const CoasterStatus& status) {
    switch (event.type) {
        case DomainEventType::DRINK_LOGGED:
            queueDrinkLogged(event.value1, event.value2, status.daily_goal_ml);
            break;
        case DomainEventType::REMINDER_ALERT:
            queueReminderAlert();
            break;
        case DomainEventType::REPEAT_ALERT:
            queueRepeatAlert();
            break;
        case DomainEventType::GOAL_REACHED:
            queueGoalAchieved(status.daily_goal_ml);
            break;
        default:
            break;
    }
}

void NetworkPush::processQueue(uint32_t now_ms) {
    if (queue_count_ == 0) return;
    if (!isConnected()) return;

    if (queue_[queue_head_].retry_count > 0 && retry_delay_ms_ > 0) {
        if (static_cast<uint32_t>(now_ms - retry_wait_start_ms_) < retry_delay_ms_) {
            return;
        }
    }

    if (has_posted_ && (static_cast<uint32_t>(now_ms - last_post_ms_) < NTFY_MIN_POST_INTERVAL_MS)) {
        return;
    }

    PushNotification& notif = queue_[queue_head_];
    bool success = executeHttpPost(notif);
    last_post_ms_ = now_ms;
    has_posted_ = true;

    if (success) {
        queue_head_ = (queue_head_ + 1) % QUEUE_CAPACITY;
        queue_count_--;
        retry_delay_ms_ = 0;
        server_reachability_ = ServerReachability::REACHABLE;
        post_success_count_++;
    } else {
        post_fail_count_++;
        notif.retry_count++;
        if (notif.retry_count <= NTFY_MAX_POST_RETRIES) {
            server_reachability_ = ServerReachability::DEGRADED;
            retry_wait_start_ms_ = now_ms;
            retry_delay_ms_ = NTFY_RETRY_BASE_MS << (notif.retry_count - 1);
        } else {
            queue_head_ = (queue_head_ + 1) % QUEUE_CAPACITY;
            queue_count_--;
            exhausted_retry_count_++;
            server_reachability_ = ServerReachability::UNREACHABLE;
            wifi_error_trigger_ = true;
            retry_delay_ms_ = 0;
        }
    }
}

void NetworkPush::buildEndpointUrl(char* out_buf, size_t buf_len) const {
    snprintf(out_buf, buf_len, "%s/%s", server_url_, topic_);
}

bool NetworkPush::executeHttpPost(const PushNotification& notif) {
#ifdef ARDUINO
    char url[128];
    buildEndpointUrl(url, sizeof(url));

    WiFiClient client;
    HTTPClient http;

    http.setTimeout(NTFY_HTTP_TIMEOUT_MS);
    http.setConnectTimeout(NTFY_HTTP_TIMEOUT_MS);

    if (!http.begin(client, url)) {
        Serial.println(F("[NET] HTTP begin failed."));
        last_http_code_ = -1;
        return false;
    }

    http.addHeader(F("Title"), notif.title);
    
    char prio_str[4];
    snprintf(prio_str, sizeof(prio_str), "%d", notif.priority);
    http.addHeader(F("Priority"), prio_str);

    if (strlen(notif.tags) > 0) {
        http.addHeader(F("Tags"), notif.tags);
    }
    http.addHeader(F("Content-Type"), F("text/plain"));

    int httpCode = http.POST(reinterpret_cast<uint8_t*>(const_cast<char*>(notif.message)), strlen(notif.message));
    last_http_code_ = httpCode;

    bool ok = (httpCode >= 200 && httpCode < 300);
    if (ok) {
        Serial.print(F("[NET] Push sent successfully: "));
        Serial.print(notif.title);
        Serial.print(F(" (HTTP "));
        Serial.print(httpCode);
        Serial.println(F(")"));
    } else {
        Serial.print(F("[NET] Push failed, HTTP Code: "));
        Serial.println(httpCode);
    }

    http.end();
    return ok;
#else
    (void)notif;
    last_http_code_ = simulated_http_code_;
    return simulated_http_success_;
#endif
}

const PushNotification* NetworkPush::peekQueue(size_t index) const {
    if (index >= queue_count_) return nullptr;
    size_t actual_idx = (queue_head_ + index) % QUEUE_CAPACITY;
    return &queue_[actual_idx];
}
