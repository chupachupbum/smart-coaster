#include <unity.h>
#include <cstring>
#include <string>
#include "coaster_domain.h"
#include "../../src/net/network_push.h"
#include "../../src/net/network_push.cpp"

static NetworkPush* net = nullptr;

void setUp(void) {
    net = new NetworkPush();
}

void tearDown(void) {
    delete net;
    net = nullptr;
}

void test_network_push_overflow_and_throttle(void) {
    net->begin("TestSSID", "TestPass", "smart-cup-topic");

    for (int i = 1; i <= 10; ++i) {
        char title[32];
        char msg[64];
        snprintf(title, sizeof(title), "Drink %d", i);
        snprintf(msg, sizeof(msg), "Consumed %d ml", i * 50);
        bool queued = net->queueNotification(title, msg);
        TEST_ASSERT_TRUE(queued);
    }

    TEST_ASSERT_EQUAL(NetworkPush::QUEUE_CAPACITY, net->getQueueSize());
    TEST_ASSERT_EQUAL_UINT32(7, net->getDroppedCount());

    const PushNotification* n0 = net->peekQueue(0);
    const PushNotification* n1 = net->peekQueue(1);
    const PushNotification* n2 = net->peekQueue(2);
    TEST_ASSERT_NOT_NULL(n0);
    TEST_ASSERT_NOT_NULL(n1);
    TEST_ASSERT_NOT_NULL(n2);
    TEST_ASSERT_EQUAL_STRING("Drink 8", n0->title);
    TEST_ASSERT_EQUAL_STRING("Drink 9", n1->title);
    TEST_ASSERT_EQUAL_STRING("Drink 10", n2->title);

    uint32_t now_ms = 10000;
    net->update(now_ms);
    TEST_ASSERT_EQUAL(2, net->getQueueSize());
    TEST_ASSERT_EQUAL_UINT32(1, net->getSuccessfulPostCount());

    now_ms += 100;
    net->update(now_ms);
    TEST_ASSERT_EQUAL(2, net->getQueueSize());
    TEST_ASSERT_EQUAL_UINT32(1, net->getSuccessfulPostCount());

    now_ms += 2000;
    net->update(now_ms);
    TEST_ASSERT_EQUAL(1, net->getQueueSize());
    TEST_ASSERT_EQUAL_UINT32(2, net->getSuccessfulPostCount());
}

void test_exponential_backoff_retry_and_recovery(void) {
    net->begin("TestSSID", "TestPass", "smart-cup-topic");
    net->update(1000);
    TEST_ASSERT_TRUE(net->isConnected());
    TEST_ASSERT_EQUAL(ServerReachability::REACHABLE, net->getServerReachability());

    net->setSimulatedHttpSuccess(false, 500);
    bool queued = net->queueNotification("Title1", "Msg1");
    TEST_ASSERT_TRUE(queued);

    // Initial attempt at t = 1000ms -> fails (retry 1 scheduled for +2000ms)
    net->update(1000);
    TEST_ASSERT_EQUAL(1, net->getQueueSize());
    TEST_ASSERT_EQUAL(ServerReachability::DEGRADED, net->getServerReachability());
    TEST_ASSERT_EQUAL_STRING("DEGRADED", net->getServerReachabilityStr());
    TEST_ASSERT_EQUAL_UINT32(1, net->getFailedPostCount());

    const PushNotification* head = net->peekQueue(0);
    TEST_ASSERT_NOT_NULL(head);
    TEST_ASSERT_EQUAL_UINT8(1, head->retry_count);

    // At t = 2000ms (1000ms elapsed < 2000ms backoff) -> not retried
    net->update(2000);
    head = net->peekQueue(0);
    TEST_ASSERT_NOT_NULL(head);
    TEST_ASSERT_EQUAL_UINT8(1, head->retry_count);

    // At t = 3000ms (2000ms backoff elapsed) -> retry 1 fails (retry 2 scheduled for +4000ms)
    net->update(3000);
    head = net->peekQueue(0);
    TEST_ASSERT_NOT_NULL(head);
    TEST_ASSERT_EQUAL_UINT8(2, head->retry_count);
    TEST_ASSERT_EQUAL(ServerReachability::DEGRADED, net->getServerReachability());

    // At t = 5000ms (2000ms elapsed < 4000ms backoff) -> not retried
    net->update(5000);
    head = net->peekQueue(0);
    TEST_ASSERT_NOT_NULL(head);
    TEST_ASSERT_EQUAL_UINT8(2, head->retry_count);

    // At t = 7000ms (4000ms backoff elapsed) -> retry 2 fails (retry 3 scheduled for +8000ms)
    net->update(7000);
    head = net->peekQueue(0);
    TEST_ASSERT_NOT_NULL(head);
    TEST_ASSERT_EQUAL_UINT8(3, head->retry_count);
    TEST_ASSERT_EQUAL(ServerReachability::DEGRADED, net->getServerReachability());

    // At t = 11000ms (4000ms elapsed < 8000ms backoff) -> not retried
    net->update(11000);
    head = net->peekQueue(0);
    TEST_ASSERT_NOT_NULL(head);
    TEST_ASSERT_EQUAL_UINT8(3, head->retry_count);

    // At t = 15000ms (8000ms backoff elapsed) -> retry 3 fails (retry 4 > 3 -> exhausted)
    net->update(15000);
    TEST_ASSERT_EQUAL(0, net->getQueueSize());
    TEST_ASSERT_EQUAL_UINT32(1, net->getExhaustedRetryCount());
    TEST_ASSERT_EQUAL(ServerReachability::UNREACHABLE, net->getServerReachability());
    TEST_ASSERT_EQUAL_STRING("UNREACHABLE", net->getServerReachabilityStr());
    TEST_ASSERT_TRUE(net->consumeWifiErrorTrigger());

    // HTTP 200 recovery: next notification succeeds and restores REACHABLE
    net->setSimulatedHttpSuccess(true, 200);
    net->queueNotification("Title2", "Msg2");
    net->update(17000); // 2000ms min post interval elapsed
    TEST_ASSERT_EQUAL(0, net->getQueueSize());
    TEST_ASSERT_EQUAL_UINT32(1, net->getSuccessfulPostCount());
    TEST_ASSERT_EQUAL(ServerReachability::REACHABLE, net->getServerReachability());
    TEST_ASSERT_EQUAL_STRING("REACHABLE", net->getServerReachabilityStr());
}

void test_boot_empty_ssid_enters_ap_fallback(void) {
    net->begin("", "");
    TEST_ASSERT_EQUAL(WifiState::AP_FALLBACK, net->getWifiState());
    TEST_ASSERT_EQUAL_STRING("AP_FALLBACK", net->getWifiStateStr());
    TEST_ASSERT_TRUE(net->isApActive());
    TEST_ASSERT_FALSE(net->isConnected());
}

void test_boot_bad_ssid_two_failures_enters_ap_fallback(void) {
    net->setHostAutoConnect(false);
    net->setSimulatedWifiOutcome(false, WifiErrorReason::TIMEOUT);
    net->begin("BadSSID", "BadPass");

    TEST_ASSERT_EQUAL(WifiState::CONNECTING, net->getWifiState());

    // 0..9999ms: attempt 1 connecting
    net->update(5000);
    TEST_ASSERT_EQUAL(WifiState::CONNECTING, net->getWifiState());

    // 10000ms: attempt 1 times out (10s) -> enters CONNECT_FAILED (5s backoff)
    net->update(10000);
    TEST_ASSERT_EQUAL(WifiState::CONNECT_FAILED, net->getWifiState());
    TEST_ASSERT_EQUAL_STRING("CONNECT_FAILED", net->getWifiStateStr());
    TEST_ASSERT_FALSE(net->isApActive());

    // 10000..14999ms: waiting in fast backoff
    net->update(14000);
    TEST_ASSERT_EQUAL(WifiState::CONNECT_FAILED, net->getWifiState());

    // 15000ms: fast retry backoff expires -> attempt 2 connecting
    net->update(15000);
    TEST_ASSERT_EQUAL(WifiState::CONNECTING, net->getWifiState());

    // 15000..24999ms: attempt 2 connecting
    net->update(20000);
    TEST_ASSERT_EQUAL(WifiState::CONNECTING, net->getWifiState());

    // 25000ms: attempt 2 times out (10s) -> 2nd failure triggers AP_FALLBACK
    net->update(25000);
    TEST_ASSERT_EQUAL(WifiState::AP_FALLBACK, net->getWifiState());
    TEST_ASSERT_EQUAL_STRING("AP_FALLBACK", net->getWifiStateStr());
    TEST_ASSERT_TRUE(net->isApActive());
    TEST_ASSERT_TRUE(net->consumeWifiErrorTrigger());
}

void test_router_recovery_in_ap_fallback(void) {
    net->setHostAutoConnect(false);
    net->setSimulatedWifiOutcome(false, WifiErrorReason::TIMEOUT);
    net->begin("HomeRouter", "pass");

    // Force to AP_FALLBACK (10s + 5s + 10s = 25s)
    net->update(10000);
    net->update(15000);
    net->update(25000);
    TEST_ASSERT_EQUAL(WifiState::AP_FALLBACK, net->getWifiState());
    TEST_ASSERT_TRUE(net->isApActive());

    // Clients connected: bg retry paused
    net->setSimulatedApClients(1);
    TEST_ASSERT_EQUAL_UINT8(1, net->getApClientCount());
    net->update(55000); // 30s later
    TEST_ASSERT_EQUAL(WifiState::AP_FALLBACK, net->getWifiState());
    TEST_ASSERT_TRUE(net->isApActive());

    // Clients disconnected: after 30s, bg retry triggers
    net->setSimulatedApClients(0);
    net->update(55000); // Reset timer anchor while client disconnected
    net->setSimulatedWifiOutcome(true, WifiErrorReason::NONE);

    // 30s elapsed with 0 clients -> triggers background reconnect (CONNECTING in AP)
    net->update(85000);
    TEST_ASSERT_EQUAL(WifiState::CONNECTING, net->getWifiState());
    TEST_ASSERT_TRUE(net->isApActive());

    // Connection completes
    net->update(85001);
    TEST_ASSERT_EQUAL(WifiState::CONNECTED, net->getWifiState());
    TEST_ASSERT_TRUE(net->isConnected());
    TEST_ASSERT_TRUE(net->isApActive()); // 3s grace period active
    TEST_ASSERT_TRUE(net->consumeWifiConnectedTrigger());

    // 2s into grace period -> AP still active
    net->update(87000);
    TEST_ASSERT_TRUE(net->isApActive());

    // 3s grace period expires -> SoftAP closed
    net->update(88001);
    TEST_ASSERT_FALSE(net->isApActive());
    TEST_ASSERT_TRUE(net->isConnected());
}

void test_safe_wifi_switch_and_rollback(void) {
    net->setHostAutoConnect(false);
    net->setSimulatedSsidFilter("NetA");
    net->setSimulatedWifiOutcome(false, WifiErrorReason::AUTH_FAILED);
    net->begin("NetA", "PassA");

    net->update(0);
    TEST_ASSERT_TRUE(net->isConnected());
    TEST_ASSERT_EQUAL_STRING("NetA", net->getSsid());

    // Request switch to NetB with bad password
    bool req = net->requestWifiSwitch("NetB", "BadPass", 1000);
    TEST_ASSERT_TRUE(req);
    TEST_ASSERT_TRUE(net->isSwitching());
    TEST_ASSERT_FALSE(net->hasRollbackOccurred());

    // 50ms elapsed (< 100ms deferral): still on NetA
    net->update(1050);
    TEST_ASSERT_TRUE(net->isSwitching());
    TEST_ASSERT_EQUAL_STRING("NetA", net->getSsid());

    // 100ms elapsed: applies NetB -> fails AUTH_FAILED -> automatic rollback to NetA
    net->update(1100);
    TEST_ASSERT_TRUE(net->hasRollbackOccurred());
    TEST_ASSERT_EQUAL_STRING("NetB", net->getFailedSsid());
    TEST_ASSERT_EQUAL_STRING("NetA", net->getSsid());
    TEST_ASSERT_TRUE(net->consumeWifiErrorTrigger());
    TEST_ASSERT_EQUAL(WifiErrorReason::AUTH_FAILED, net->getLastError());
    TEST_ASSERT_EQUAL_STRING("AUTH_FAILED", net->getLastErrorStr());

    // Reconnection to NetA completes
    net->update(1101);
    TEST_ASSERT_TRUE(net->isConnected());
    TEST_ASSERT_TRUE(net->hasRollbackOccurred());
    TEST_ASSERT_EQUAL_STRING("NetA", net->getSsid());
}

void test_scan_deduplication_and_rssi_sorting(void) {
    WifiScanEntry raw[] = {
        {"CoffeeShop", -80, 6, false},
        {"", -50, 1, true},
        {"HomeWiFi", -70, 11, false},
        {"CoffeeShop", -60, 6, false},
        {"WeakAP", -95, 1, false},
        {"HomeWiFi", -75, 11, false},
        {"StrongAP", -30, 36, true}
    };

    net->ingestScanEntries(raw, sizeof(raw) / sizeof(raw[0]));

    TEST_ASSERT_EQUAL(4, net->getScanCount());
    const WifiScanEntry* results = net->getScanResults();
    TEST_ASSERT_NOT_NULL(results);

    // Sorted descending by RSSI
    TEST_ASSERT_EQUAL_STRING("StrongAP", results[0].ssid);
    TEST_ASSERT_EQUAL_INT32(-30, results[0].rssi);
    TEST_ASSERT_TRUE(results[0].auth_open);

    TEST_ASSERT_EQUAL_STRING("CoffeeShop", results[1].ssid);
    TEST_ASSERT_EQUAL_INT32(-60, results[1].rssi);

    TEST_ASSERT_EQUAL_STRING("HomeWiFi", results[2].ssid);
    TEST_ASSERT_EQUAL_INT32(-70, results[2].rssi);

    TEST_ASSERT_EQUAL_STRING("WeakAP", results[3].ssid);
    TEST_ASSERT_EQUAL_INT32(-95, results[3].rssi);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_network_push_overflow_and_throttle);
    RUN_TEST(test_exponential_backoff_retry_and_recovery);
    RUN_TEST(test_boot_empty_ssid_enters_ap_fallback);
    RUN_TEST(test_boot_bad_ssid_two_failures_enters_ap_fallback);
    RUN_TEST(test_router_recovery_in_ap_fallback);
    RUN_TEST(test_safe_wifi_switch_and_rollback);
    RUN_TEST(test_scan_deduplication_and_rssi_sorting);
    return UNITY_END();
}
