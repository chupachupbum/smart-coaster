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
#include <Preferences.h>
#endif

#include "config.h"
#include "coaster_domain.h"

enum class WifiState : uint8_t {
    DISCONNECTED = 0,
    CONNECTING,
    CONNECTED,
    CONNECT_FAILED,
    AP_FALLBACK
};

inline const char* wifiStateToString(WifiState state) {
    switch (state) {
        case WifiState::DISCONNECTED:   return "DISCONNECTED";
        case WifiState::CONNECTING:     return "CONNECTING";
        case WifiState::CONNECTED:      return "CONNECTED";
        case WifiState::CONNECT_FAILED: return "CONNECT_FAILED";
        case WifiState::AP_FALLBACK:    return "AP_FALLBACK";
        default:                        return "UNKNOWN";
    }
}

enum class ServerReachability : uint8_t {
    UNKNOWN = 0,
    REACHABLE,
    DEGRADED,
    UNREACHABLE
};

inline const char* serverReachabilityToString(ServerReachability s) {
    switch (s) {
        case ServerReachability::UNKNOWN:     return "UNKNOWN";
        case ServerReachability::REACHABLE:   return "REACHABLE";
        case ServerReachability::DEGRADED:    return "DEGRADED";
        case ServerReachability::UNREACHABLE: return "UNREACHABLE";
        default:                              return "UNKNOWN";
    }
}

enum class WifiErrorReason : uint8_t {
    NONE = 0,
    TIMEOUT,
    AUTH_FAILED,
    SSID_NOT_FOUND
};

inline const char* wifiErrorReasonToString(WifiErrorReason r) {
    switch (r) {
        case WifiErrorReason::NONE:           return "NONE";
        case WifiErrorReason::TIMEOUT:        return "TIMEOUT";
        case WifiErrorReason::AUTH_FAILED:    return "AUTH_FAILED";
        case WifiErrorReason::SSID_NOT_FOUND: return "SSID_NOT_FOUND";
        default:                              return "NONE";
    }
}

struct WifiScanEntry {
    char ssid[33];
    int32_t rssi;
    uint8_t channel;
    bool auth_open;
};

struct PushNotification {
    char title[48];
    char message[128];
    int priority;
    char tags[48];
    uint32_t timestamp_ms;
    uint8_t retry_count;
};

class NetworkPush {
public:
    static constexpr size_t QUEUE_CAPACITY = NTFY_MAX_QUEUE_CAPACITY;
    static constexpr size_t MAX_SCAN_RESULTS = WIFI_SCAN_MAX_RESULTS;

    NetworkPush();
    ~NetworkPush() = default;

    void begin(const char* ssid = WIFI_SSID,
               const char* password = WIFI_PASSWORD,
               const char* ntfy_topic = NTFY_DEFAULT_TOPIC,
               const char* server_url = NTFY_SERVER_URL);

    void update(uint32_t now_ms);

    bool queueNotification(const char* title, const char* message, int priority = 3, const char* tags = "");
    bool queueDrinkLogged(float drink_delta_ml, float total_consumed_ml, float daily_goal_ml);
    bool queueReminderAlert();
    bool queueRepeatAlert();
    bool queueGoalAchieved(float daily_goal_ml);
    void handleDomainEvent(const DomainEvent& event, const CoasterStatus& status);

    bool isConnected() const { return wifi_state_ == WifiState::CONNECTED; }
    WifiState getWifiState() const { return wifi_state_; }
    const char* getWifiStateStr() const { return wifiStateToString(wifi_state_); }

    size_t getQueueSize() const { return queue_count_; }
    bool isQueueFull() const { return queue_count_ >= QUEUE_CAPACITY; }
    bool isQueueEmpty() const { return queue_count_ == 0; }

    uint32_t getSuccessfulPostCount() const { return post_success_count_; }
    uint32_t getFailedPostCount() const { return post_fail_count_; }
    uint32_t getDroppedCount() const { return dropped_count_; }
    uint32_t getExhaustedRetryCount() const { return exhausted_retry_count_; }
    int getLastHttpCode() const { return last_http_code_; }

    ServerReachability getServerReachability() const { return server_reachability_; }
    const char* getServerReachabilityStr() const { return serverReachabilityToString(server_reachability_); }

    bool saveConfigToNvs();
    void forgetCredentials(uint32_t now_ms = 0);

    bool requestWifiSwitch(const char* new_ssid, const char* new_pass, uint32_t now_ms);
    bool isSwitching() const { return switch_pending_ || switching_in_progress_; }
    bool hasRollbackOccurred() const { return rollback_occurred_; }
    WifiErrorReason getLastError() const { return last_error_; }
    const char* getLastErrorStr() const { return wifiErrorReasonToString(last_error_); }
    const char* getFailedSsid() const { return failed_ssid_; }

    const char* getSsid() const { return ssid_; }
    const char* getTopic() const { return topic_; }
    const char* getServerUrl() const { return server_url_; }
    void getIpString(char* out, size_t len) const;
    int32_t getRssi() const;
    void getMacString(char* out, size_t len) const;

    void startApFallback(uint32_t now_ms);
    bool isApActive() const { return ap_active_; }
    uint8_t getApClientCount() const;

    bool consumeWifiConnectedTrigger();
    bool consumeWifiErrorTrigger();

    bool startAsyncScan(uint32_t now_ms);
    void pollAsyncScan(uint32_t now_ms);
    bool isScanning() const { return scanning_; }
    size_t getScanCount() const { return scan_count_; }
    const WifiScanEntry* getScanResults() const { return scan_results_; }
    uint32_t getLastScanMs() const { return last_scan_ms_; }
    void ingestScanEntries(const WifiScanEntry* raw, size_t count);

    void setCredentials(const char* ssid, const char* password);
    void setTopic(const char* ntfy_topic);
    void setServerUrl(const char* server_url);
    void disconnect();
    void reconnect(uint32_t now_ms = 0);

    const PushNotification* peekQueue(size_t index) const;

    void setHostAutoConnect(bool enable) { host_auto_connect_ = enable; }
    void setSimulatedWifiOutcome(bool should_connect, WifiErrorReason fail_reason = WifiErrorReason::AUTH_FAILED);
    void setSimulatedSsidFilter(const char* good_ssid);
    void setSimulatedHttpSuccess(bool ok, int http_code = 200);
    void setSimulatedApClients(uint8_t count) { simulated_ap_clients_ = count; }
    void setSimulatedScanResults(const WifiScanEntry* entries, size_t count);

private:
    char ssid_[33];
    char password_[65];
    char prev_ssid_[33];
    char prev_password_[65];
    char pending_ssid_[33];
    char pending_pass_[65];
    char failed_ssid_[33];
    char topic_[64];
    char server_url_[64];

    WifiState wifi_state_;
    ServerReachability server_reachability_;
    WifiErrorReason last_error_;
    uint8_t consecutive_sta_failures_;

    bool ap_active_;
    bool ap_shutdown_pending_;
    bool switch_pending_;
    bool switching_in_progress_;
    bool rollback_occurred_;

    bool wifi_connected_trigger_;
    bool wifi_error_trigger_;

    uint32_t state_entry_ms_;
    uint32_t retry_backoff_ms_;
    uint32_t switch_armed_ms_;
    uint32_t ap_shutdown_armed_ms_;
    uint32_t last_ap_bg_retry_ms_;

    PushNotification queue_[QUEUE_CAPACITY];
    size_t queue_head_;
    size_t queue_count_;

    bool has_posted_;
    uint32_t last_post_ms_;
    uint32_t retry_wait_start_ms_;
    uint32_t retry_delay_ms_;
    uint32_t post_success_count_;
    uint32_t post_fail_count_;
    uint32_t dropped_count_;
    uint32_t exhausted_retry_count_;
    int last_http_code_;

    bool scanning_;
    uint32_t scan_start_ms_;
    uint32_t last_scan_ms_;
    size_t scan_count_;
    WifiScanEntry scan_results_[MAX_SCAN_RESULTS];

    bool host_auto_connect_;
    bool simulated_connect_ok_;
    WifiErrorReason simulated_fail_reason_;
    char simulated_good_ssid_[33];
    bool simulated_http_success_;
    int simulated_http_code_;
    uint8_t simulated_ap_clients_;

    void tickWifi(uint32_t now_ms);
    void processQueue(uint32_t now_ms);
    bool executeHttpPost(const PushNotification& notif);
    void buildEndpointUrl(char* out_buf, size_t buf_len) const;
    void startStaConnect(uint32_t now_ms);
};

#endif // NET_NETWORK_PUSH_H_
