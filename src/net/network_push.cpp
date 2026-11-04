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
      state_entry_ms_(0),
      retry_backoff_ms_(WIFI_RETRY_INTERVAL_MS),
      queue_head_(0),
      queue_count_(0),
      last_post_ms_(0),
      post_success_count_(0),
      post_fail_count_(0),
      dropped_count_(0),
      last_http_code_(0) {
    ssid_[0] = '\0';
    password_[0] = '\0';
    topic_[0] = '\0';
    server_url_[0] = '\0';
    memset(queue_, 0, sizeof(queue_));
}

void NetworkPush::begin(const char* ssid, const char* password, const char* ntfy_topic, const char* server_url) {
    setCredentials(ssid, password);
    setTopic(ntfy_topic);
    setServerUrl(server_url);

    retry_backoff_ms_ = WIFI_RETRY_INTERVAL_MS;
    queue_head_ = 0;
    queue_count_ = 0;
    last_post_ms_ = 0;

#ifdef ARDUINO
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false); // Managed explicitly by state machine for backoff control
#endif

    reconnect();
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

void NetworkPush::reconnect() {
    if (strlen(ssid_) == 0) {
        wifi_state_ = WifiState::DISCONNECTED;
        return;
    }

#ifdef ARDUINO
    WiFi.disconnect(false);
    WiFi.begin(ssid_, strlen(password_) > 0 ? password_ : nullptr);
    state_entry_ms_ = millis();
#else
    state_entry_ms_ = 0;
#endif
    wifi_state_ = WifiState::CONNECTING;
}

void NetworkPush::update(uint32_t now_ms) {
    tickWifi(now_ms);
    processQueue(now_ms);
}

void NetworkPush::tickWifi(uint32_t now_ms) {
#ifdef ARDUINO
    wl_status_t status = WiFi.status();

    switch (wifi_state_) {
        case WifiState::DISCONNECTED:
            // Check if backoff delay elapsed to attempt reconnect
            if (now_ms - state_entry_ms_ >= retry_backoff_ms_) {
                reconnect();
                state_entry_ms_ = now_ms;
            }
            break;

        case WifiState::CONNECTING:
            if (status == WL_CONNECTED) {
                wifi_state_ = WifiState::CONNECTED;
                state_entry_ms_ = now_ms;
                retry_backoff_ms_ = WIFI_RETRY_INTERVAL_MS; // Reset backoff on success
                Serial.print(F("[NET] Wi-Fi Connected! IP: "));
                Serial.println(WiFi.localIP());
            } else if (status == WL_CONNECT_FAILED || status == WL_NO_SSID_AVAIL ||
                       (now_ms - state_entry_ms_ >= WIFI_CONNECT_TIMEOUT_MS)) {
                wifi_state_ = WifiState::CONNECT_FAILED;
                state_entry_ms_ = now_ms;
                WiFi.disconnect(false);
                // Exponential backoff with clamp
                retry_backoff_ms_ = (retry_backoff_ms_ * 3) / 2;
                if (retry_backoff_ms_ > WIFI_MAX_RETRY_BACKOFF_MS) {
                    retry_backoff_ms_ = WIFI_MAX_RETRY_BACKOFF_MS;
                }
                Serial.print(F("[NET] Wi-Fi Connection Failed. Retrying in "));
                Serial.print(retry_backoff_ms_);
                Serial.println(F(" ms"));
            }
            break;

        case WifiState::CONNECTED:
            if (status != WL_CONNECTED) {
                wifi_state_ = WifiState::DISCONNECTED;
                state_entry_ms_ = now_ms;
                Serial.println(F("[NET] Wi-Fi Connection Lost."));
            }
            break;

        case WifiState::CONNECT_FAILED:
            if (now_ms - state_entry_ms_ >= retry_backoff_ms_) {
                reconnect();
                state_entry_ms_ = now_ms;
            }
            break;
    }
#else
    // Host / simulated test fallback
    if (wifi_state_ == WifiState::CONNECTING) {
        wifi_state_ = WifiState::CONNECTED;
        state_entry_ms_ = now_ms;
    }
#endif
}

bool NetworkPush::queueNotification(const char* title, const char* message, int priority, const char* tags) {
    if (!title || !message || strlen(title) == 0 || strlen(message) == 0) {
        return false;
    }

    // Overflow protection: Drop oldest item if queue is full
    if (queue_count_ >= QUEUE_CAPACITY) {
        queue_head_ = (queue_head_ + 1) % QUEUE_CAPACITY;
        queue_count_--;
        dropped_count_++;
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
    if (wifi_state_ != WifiState::CONNECTED) return;

    // Rate-limiting: throttle consecutive HTTP POSTs
    if (last_post_ms_ > 0 && (now_ms - last_post_ms_ < NTFY_MIN_POST_INTERVAL_MS)) {
        return;
    }

    const PushNotification& notif = queue_[queue_head_];
    bool success = executeHttpPost(notif);

    // Dequeue immediately to prevent head-of-line blocking
    queue_head_ = (queue_head_ + 1) % QUEUE_CAPACITY;
    queue_count_--;
    last_post_ms_ = now_ms;

    if (success) {
        post_success_count_++;
    } else {
        post_fail_count_++;
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
    // Host mock execution
    (void)notif;
    last_http_code_ = 200;
    return true;
#endif
}

const PushNotification* NetworkPush::peekQueue(size_t index) const {
    if (index >= queue_count_) return nullptr;
    size_t actual_idx = (queue_head_ + index) % QUEUE_CAPACITY;
    return &queue_[actual_idx];
}
