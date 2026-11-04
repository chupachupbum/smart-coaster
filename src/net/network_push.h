#ifndef NET_NETWORK_PUSH_H_
#define NET_NETWORK_PUSH_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef ARDUINO
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#endif

#include "config.h"
#include "coaster_domain.h"

/**
 * @brief Discrete connection states of the Wi-Fi state machine.
 */
enum class WifiState : uint8_t {
    DISCONNECTED = 0,   // Radio idle or disconnected, awaiting reconnect timer
    CONNECTING,         // Connection in progress, non-blocking polling
    CONNECTED,          // Associated with AP and IP address assigned
    CONNECT_FAILED      // Connection attempt timed out or rejected, in backoff
};

/**
 * @brief Convert WifiState enum to human-readable string (used in telemetry & logging).
 */
inline const char* wifiStateToString(WifiState state) {
    switch (state) {
        case WifiState::DISCONNECTED:   return "DISCONNECTED";
        case WifiState::CONNECTING:     return "CONNECTING";
        case WifiState::CONNECTED:      return "CONNECTED";
        case WifiState::CONNECT_FAILED: return "CONNECT_FAILED";
        default:                        return "UNKNOWN";
    }
}

/**
 * @brief Statically-allocated push notification item.
 * Zero dynamic heap allocation to prevent heap fragmentation on ESP32-C3.
 */
struct PushNotification {
    char title[48];
    char message[128];
    int priority;
    char tags[48];
    uint32_t timestamp_ms;
};

/**
 * @brief Non-blocking Wi-Fi manager and ntfy.sh HTTP push notification client.
 *
 * Architecture Highlights:
 * 1. Non-blocking Wi-Fi State Machine: Never blocks the cooperative scheduler.
 *    Connection and reconnection backoff are evaluated using timestamp deltas.
 * 2. Asynchronous Notification Queue: Ring buffer capped at NTFY_MAX_QUEUE_CAPACITY (3).
 *    Drops oldest item on overflow to preserve newest hydration events and fixed memory.
 * 3. Rate-Limited HTTP Dispatch: Minimum NTFY_MIN_POST_INTERVAL_MS (2000ms) between POSTs
 *    to prevent hitting ntfy.sh public rate limiters.
 * 4. Strict Socket Timeouts: 3000ms timeout on HTTPClient to prevent network hangs.
 */
class NetworkPush {
public:
    static constexpr size_t QUEUE_CAPACITY = NTFY_MAX_QUEUE_CAPACITY;

    NetworkPush();
    ~NetworkPush() = default;

    /**
     * @brief Initialize Wi-Fi credentials, ntfy.sh topic, and state machine.
     * Starts the initial non-blocking connection attempt immediately.
     */
    void begin(const char* ssid = WIFI_SSID,
               const char* password = WIFI_PASSWORD,
               const char* ntfy_topic = NTFY_DEFAULT_TOPIC,
               const char* server_url = NTFY_SERVER_URL);

    /**
     * @brief Non-blocking periodic tick called from main cooperative loop.
     * Progresses Wi-Fi connection state machine and dispatches queued notifications.
     * @param now_ms Current system timestamp in milliseconds.
     */
    void update(uint32_t now_ms);

    /**
     * @brief Queue a raw notification to be sent to ntfy.sh.
     * If the queue is full, the oldest un-dispatched notification is dropped.
     *
     * @param title Title of notification (max 47 chars).
     * @param message Body text of notification (max 127 chars).
     * @param priority Notification priority 1..5 (3 = default, 4 = high).
     * @param tags Comma-separated emoji/tags (e.g. "glass_of_milk,droplet").
     * @return true if queued successfully, false if title or message is null/empty.
     */
    bool queueNotification(const char* title, const char* message, int priority = 3, const char* tags = "");

    /**
     * @brief High-level helper: queue drink logged notification matching E2E assertions.
     */
    bool queueDrinkLogged(float drink_delta_ml, float total_consumed_ml, float daily_goal_ml);

    /**
     * @brief High-level helper: queue primary reminder alert notification.
     */
    bool queueReminderAlert();

    /**
     * @brief High-level helper: queue repeating reminder alert notification.
     */
    bool queueRepeatAlert();

    /**
     * @brief High-level helper: queue daily goal achieved notification.
     */
    bool queueGoalAchieved(float daily_goal_ml);

    /**
     * @brief Dispatch helper: maps DomainEvent to appropriate push notification.
     */
    void handleDomainEvent(const DomainEvent& event, const CoasterStatus& status);

    // --- State & Status Queries ---
    bool isConnected() const { return wifi_state_ == WifiState::CONNECTED; }
    WifiState getWifiState() const { return wifi_state_; }
    const char* getWifiStateStr() const { return wifiStateToString(wifi_state_); }

    size_t getQueueSize() const { return queue_count_; }
    bool isQueueFull() const { return queue_count_ >= QUEUE_CAPACITY; }
    bool isQueueEmpty() const { return queue_count_ == 0; }

    uint32_t getSuccessfulPostCount() const { return post_success_count_; }
    uint32_t getFailedPostCount() const { return post_fail_count_; }
    uint32_t getDroppedCount() const { return dropped_count_; }
    int getLastHttpCode() const { return last_http_code_; }

    // --- Configuration Mutators ---
    void setCredentials(const char* ssid, const char* password);
    void setTopic(const char* ntfy_topic);
    void setServerUrl(const char* server_url);
    void disconnect();
    void reconnect();

    // Inspection for testing / debugging
    const PushNotification* peekQueue(size_t index) const;

private:
    char ssid_[33];
    char password_[65];
    char topic_[64];
    char server_url_[64];

    WifiState wifi_state_;
    uint32_t state_entry_ms_;
    uint32_t retry_backoff_ms_;

    // Static ring buffer
    PushNotification queue_[QUEUE_CAPACITY];
    size_t queue_head_;
    size_t queue_count_;

    // Rate limiting & metrics
    uint32_t last_post_ms_;
    uint32_t post_success_count_;
    uint32_t post_fail_count_;
    uint32_t dropped_count_;
    int last_http_code_;

    void tickWifi(uint32_t now_ms);
    void processQueue(uint32_t now_ms);
    bool executeHttpPost(const PushNotification& notif);
    void buildEndpointUrl(char* out_buf, size_t buf_len) const;
};

#endif // NET_NETWORK_PUSH_H_

