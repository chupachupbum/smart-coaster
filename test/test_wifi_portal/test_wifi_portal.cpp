#include <unity.h>
#include <cstring>
#include <string>

#include "coaster_domain.h"
#include "../../lib/coaster_domain/coaster_domain.cpp"
#include "../../src/hal/led_ring.h"
#include "../../src/hal/led_ring.cpp"
#include "../../src/net/network_push.h"
#include "../../src/net/network_push.cpp"
#include "../../src/net/wifi_portal.h"
#include "../../src/net/wifi_portal.cpp"
#include "../../src/telemetry/telemetry_console.h"
#include "../../src/telemetry/telemetry_console.cpp"

static CoasterDomain* domain = nullptr;
static NetworkPush* network = nullptr;
static LedRing* ring = nullptr;
static WifiPortal* portal = nullptr;
static TelemetryConsole* console = nullptr;

void setUp(void) {
    domain = new CoasterDomain(2000.0f);
    network = new NetworkPush();
    network->begin("InitialSSID", "InitialPass", "smart-cup-stand", "http://ntfy.sh");
    ring = new LedRing(PIN_LED_RING, 16, 80);
    ring->begin();
    portal = new WifiPortal(*domain, *network, *ring);
    console = new TelemetryConsole();
    console->begin();
    console->setNetworkPush(network);
    console->setWifiStatus(network->getWifiStateStr());
    console->setServerStatus(network->getServerReachabilityStr());
}

void tearDown(void) {
    delete console;
    delete portal;
    delete ring;
    delete network;
    delete domain;
    console = nullptr;
    portal = nullptr;
    ring = nullptr;
    network = nullptr;
    domain = nullptr;
}

void test_route_root_index_html(void) {
    char out_buf[8192];
    const char* content_type = nullptr;

    int code = portal->handleRequest("GET", "/", "", out_buf, sizeof(out_buf), 1000, &content_type);
    TEST_ASSERT_EQUAL(200, code);
    TEST_ASSERT_NOT_NULL(content_type);
    TEST_ASSERT_EQUAL_STRING("text/html", content_type);
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "<!DOCTYPE html>"));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "Smart Coaster"));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "smart-coaster.local"));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "Wi-Fi Networks"));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "Live Coaster"));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "Notifications & Goal"));

    // POST / must return 405 Method Not Allowed
    code = portal->handleRequest("POST", "/", "", out_buf, sizeof(out_buf), 1000, &content_type);
    TEST_ASSERT_EQUAL(405, code);
}

void test_route_captive_portal_redirects(void) {
    char out_buf[512];
    const char* probes[] = {
        "/generate_204",
        "/hotspot-detect.html",
        "/connecttest.txt",
        "/ncsi.txt",
        "/redirect"
    };

    for (const char* path : probes) {
        const char* content_type = nullptr;
        int code = portal->handleRequest("GET", path, "", out_buf, sizeof(out_buf), 1000, &content_type);
        TEST_ASSERT_EQUAL(302, code);
        TEST_ASSERT_NOT_NULL(strstr(out_buf, "192.168.4.1"));
    }
}

void test_route_api_status(void) {
    char out_buf[1024];
    const char* content_type = nullptr;

    int code = portal->handleRequest("GET", "/api/status", "", out_buf, sizeof(out_buf), 1000, &content_type);
    TEST_ASSERT_EQUAL(200, code);
    TEST_ASSERT_NOT_NULL(content_type);
    TEST_ASSERT_EQUAL_STRING("application/json", content_type);

    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"cup_state\":\"EMPTY\""));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"alert_state\":\"IDLE\""));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"net_weight_g\":0.0"));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"consumed_ml\":0.0"));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"daily_goal_ml\":2000.0"));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"progress_pct\":0.0"));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"demo_mode\":false"));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"led_mode\":"));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"wifi_state\":"));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"ssid\":\"InitialSSID\""));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"ip\":"));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"rssi\":"));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"mdns\":\"smart-coaster.local\""));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"ap_active\":"));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"server_status\":"));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"queue_size\":0"));

    // POST /api/status is not allowed
    code = portal->handleRequest("POST", "/api/status", "", out_buf, sizeof(out_buf), 1000, &content_type);
    TEST_ASSERT_EQUAL(405, code);
}

void test_route_api_wifi_status(void) {
    char out_buf[1024];
    const char* content_type = nullptr;

    int code = portal->handleRequest("GET", "/api/wifi/status", "", out_buf, sizeof(out_buf), 1000, &content_type);
    TEST_ASSERT_EQUAL(200, code);
    TEST_ASSERT_NOT_NULL(content_type);
    TEST_ASSERT_EQUAL_STRING("application/json", content_type);

    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"wifi_state\":"));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"ssid\":\"InitialSSID\""));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"ip\":"));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"rssi\":"));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"mac\":"));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"mdns_host\":\"smart-coaster\""));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"ap_active\":"));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"ap_ssid\":\"SmartCoaster-Setup\""));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"ap_ip\":\"192.168.4.1\""));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"ap_clients\":0"));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"switching\":false"));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"rollback_occurred\":false"));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"failed_ssid\":\"\""));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"last_error\":\"NONE\""));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"server_status\":"));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"post_ok\":0"));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"post_fail\":0"));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"post_retries_exhausted\":0"));
}

void test_route_api_wifi_scan(void) {
    char out_buf[1024];
    const char* content_type = nullptr;

    WifiScanEntry mock_entries[2] = {
        {"Office-WiFi", -55, 6, false},
        {"Coffee\"Shop", -72, 11, true} // test string escaping
    };
    network->setSimulatedScanResults(mock_entries, 2);

    int code = portal->handleRequest("GET", "/api/wifi/scan", "", out_buf, sizeof(out_buf), 1000, &content_type);
    TEST_ASSERT_EQUAL(200, code);
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"count\":2"));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"ssid\":\"Office-WiFi\""));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"rssi\":-55"));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"open\":false"));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"ssid\":\"Coffee\\\"Shop\""));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"open\":true"));

    // POST /api/wifi/scan triggers async scan
    code = portal->handleRequest("POST", "/api/wifi/scan", "", out_buf, sizeof(out_buf), 1100, &content_type);
    TEST_ASSERT_EQUAL(200, code);
    TEST_ASSERT_TRUE(network->isScanning());
}

void test_route_api_wifi_connect_json_and_urlencoded(void) {
    char out_buf[512];
    const char* content_type = nullptr;

    // 1. JSON POST
    const char* json_body = "{\"ssid\":\"HomeNetwork\",\"password\":\"SecretPass123\"}";
    int code = portal->handleRequest("POST", "/api/wifi/connect", json_body, out_buf, sizeof(out_buf), 1000, &content_type);
    TEST_ASSERT_EQUAL(200, code);
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"status\":\"CONNECTING\""));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"ssid\":\"HomeNetwork\""));
    TEST_ASSERT_TRUE(network->isSwitching());

    // 2. Form-urlencoded POST with URL decoding (+ -> space, %21 -> !)
    network->begin("ResetSSID", "ResetPass"); // reset state
    const char* form_body = "ssid=My+Guest%20Network&password=pass%21word";
    code = portal->handleRequest("POST", "/api/wifi/connect", form_body, out_buf, sizeof(out_buf), 2000, &content_type);
    TEST_ASSERT_EQUAL(200, code);
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"status\":\"CONNECTING\""));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"ssid\":\"My Guest Network\""));
    TEST_ASSERT_TRUE(network->isSwitching());

    // 3. Validation errors
    code = portal->handleRequest("POST", "/api/wifi/connect", "{}", out_buf, sizeof(out_buf), 3000, &content_type);
    TEST_ASSERT_EQUAL(400, code);
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"status\":\"ERROR\""));

    code = portal->handleRequest("POST", "/api/wifi/connect", "ssid=", out_buf, sizeof(out_buf), 4000, &content_type);
    TEST_ASSERT_EQUAL(400, code);

    // Oversized SSID > 32 chars
    std::string huge_ssid = "ssid=" + std::string(35, 'A') + "&password=test";
    code = portal->handleRequest("POST", "/api/wifi/connect", huge_ssid.c_str(), out_buf, sizeof(out_buf), 5000, &content_type);
    TEST_ASSERT_EQUAL(400, code);
}

void test_route_api_wifi_ap_and_forget(void) {
    char out_buf[512];
    const char* content_type = nullptr;

    TEST_ASSERT_FALSE(network->isApActive());
    int code = portal->handleRequest("POST", "/api/wifi/ap", "", out_buf, sizeof(out_buf), 1000, &content_type);
    TEST_ASSERT_EQUAL(200, code);
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"status\":\"AP_FALLBACK\""));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"ap_ssid\":\"SmartCoaster-Setup\""));
    TEST_ASSERT_TRUE(network->isApActive());

    code = portal->handleRequest("POST", "/api/wifi/forget", "", out_buf, sizeof(out_buf), 2000, &content_type);
    TEST_ASSERT_EQUAL(200, code);
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"status\":\"FORGOTTEN\""));
    TEST_ASSERT_TRUE(network->isApActive());
    TEST_ASSERT_EQUAL_STRING("", network->getSsid());
}

void test_route_api_config(void) {
    char out_buf[512];
    const char* content_type = nullptr;

    const char* cfg_body = "{\"daily_goal_ml\":2500,\"demo_mode\":true,\"ntfy_topic\":\"new-topic\",\"ntfy_server_url\":\"http://custom-ntfy.local\"}";
    int code = portal->handleRequest("POST", "/api/config", cfg_body, out_buf, sizeof(out_buf), 1000, &content_type);
    TEST_ASSERT_EQUAL(200, code);
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"status\":\"OK\""));
    TEST_ASSERT_EQUAL_FLOAT(2500.0f, domain->get_status().daily_goal_ml);
    TEST_ASSERT_TRUE(domain->is_demo_mode());
    TEST_ASSERT_TRUE(ring->isDemoMode());
    TEST_ASSERT_EQUAL_STRING("new-topic", network->getTopic());
    TEST_ASSERT_EQUAL_STRING("http://custom-ntfy.local", network->getServerUrl());

    // Invalid goal: below 100 ml
    code = portal->handleRequest("POST", "/api/config", "{\"daily_goal_ml\":50}", out_buf, sizeof(out_buf), 1000, &content_type);
    TEST_ASSERT_EQUAL(400, code);

    // Invalid goal: above 10000 ml
    code = portal->handleRequest("POST", "/api/config", "{\"daily_goal_ml\":15000}", out_buf, sizeof(out_buf), 1000, &content_type);
    TEST_ASSERT_EQUAL(400, code);
}

void test_route_api_action(void) {
    char out_buf[512];
    const char* content_type = nullptr;

    // Tare action
    domain->update(150.0f, 1000);
    int code = portal->handleRequest("POST", "/api/action", "{\"action\":\"tare\"}", out_buf, sizeof(out_buf), 1000, &content_type);
    TEST_ASSERT_EQUAL(200, code);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, domain->get_status().net_weight_g);

    // Reset action
    code = portal->handleRequest("POST", "/api/action", "{\"action\":\"reset\"}", out_buf, sizeof(out_buf), 1000, &content_type);
    TEST_ASSERT_EQUAL(200, code);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, domain->get_status().consumed_today_ml);

    // Demo toggle actions
    code = portal->handleRequest("POST", "/api/action", "{\"action\":\"demo_on\"}", out_buf, sizeof(out_buf), 1000, &content_type);
    TEST_ASSERT_EQUAL(200, code);
    TEST_ASSERT_TRUE(domain->is_demo_mode());

    code = portal->handleRequest("POST", "/api/action", "{\"action\":\"demo_off\"}", out_buf, sizeof(out_buf), 1000, &content_type);
    TEST_ASSERT_EQUAL(200, code);
    TEST_ASSERT_FALSE(domain->is_demo_mode());

    // Test push action
    size_t q_before = network->getQueueSize();
    code = portal->handleRequest("POST", "/api/action", "{\"action\":\"test_push\"}", out_buf, sizeof(out_buf), 1000, &content_type);
    TEST_ASSERT_EQUAL(200, code);
    TEST_ASSERT_EQUAL(q_before + 1, network->getQueueSize());

    // Unknown action -> 400
    code = portal->handleRequest("POST", "/api/action", "{\"action\":\"invalid_action\"}", out_buf, sizeof(out_buf), 1000, &content_type);
    TEST_ASSERT_EQUAL(400, code);
}

void test_telemetry_console_wifi_status_and_scan(void) {
    char out_buf[512];

    size_t len = console->executeCommand("WIFI STATUS", *domain, out_buf, sizeof(out_buf), 1000);
    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"status\":\"OK\""));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"command\":\"WIFI STATUS\""));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"ssid\":\"InitialSSID\""));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"ap_active\":false"));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"server_status\":"));

    len = console->executeCommand("WIFI SCAN", *domain, out_buf, sizeof(out_buf), 1000);
    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"status\":\"OK\""));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"command\":\"WIFI SCAN\""));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"networks\":["));
}

void test_telemetry_console_wifi_set_quoted_and_unquoted(void) {
    char out_buf[512];

    // 1. Quoted SSID with spaces
    size_t len = console->executeCommand("WIFI SET \"My Coffee WiFi\" mypass123", *domain, out_buf, sizeof(out_buf), 1000);
    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"status\":\"OK\""));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"command\":\"WIFI SET\""));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"ssid\":\"My Coffee WiFi\""));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"switching\":true"));

    // 2. Unquoted SSID
    network->begin("ResetSSID", "ResetPass");
    len = console->executeCommand("WIFI SET OfficeGuest guestpass", *domain, out_buf, sizeof(out_buf), 2000);
    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"status\":\"OK\""));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"ssid\":\"OfficeGuest\""));

    // 3. Open network (no password)
    network->begin("ResetSSID", "ResetPass");
    len = console->executeCommand("WIFI SET \"Public Free WiFi\"", *domain, out_buf, sizeof(out_buf), 3000);
    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"status\":\"OK\""));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"ssid\":\"Public Free WiFi\""));

    // 4. Missing SSID
    len = console->executeCommand("WIFI SET", *domain, out_buf, sizeof(out_buf), 4000);
    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"status\":\"ERROR\""));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"msg\":\"Missing SSID\""));
}

void test_telemetry_console_wifi_ap_forget_and_null_guard(void) {
    char out_buf[512];

    size_t len = console->executeCommand("WIFI AP", *domain, out_buf, sizeof(out_buf), 1000);
    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"status\":\"OK\""));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"command\":\"WIFI AP\""));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"ap_active\":true"));
    TEST_ASSERT_TRUE(network->isApActive());

    len = console->executeCommand("WIFI FORGET", *domain, out_buf, sizeof(out_buf), 2000);
    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"status\":\"OK\""));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"command\":\"WIFI FORGET\""));

    // Null network guard
    console->setNetworkPush(nullptr);
    len = console->executeCommand("WIFI STATUS", *domain, out_buf, sizeof(out_buf), 3000);
    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"status\":\"ERROR\""));
    TEST_ASSERT_NOT_NULL(strstr(out_buf, "\"msg\":\"Network manager unavailable\""));
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_route_root_index_html);
    RUN_TEST(test_route_captive_portal_redirects);
    RUN_TEST(test_route_api_status);
    RUN_TEST(test_route_api_wifi_status);
    RUN_TEST(test_route_api_wifi_scan);
    RUN_TEST(test_route_api_wifi_connect_json_and_urlencoded);
    RUN_TEST(test_route_api_wifi_ap_and_forget);
    RUN_TEST(test_route_api_config);
    RUN_TEST(test_route_api_action);
    RUN_TEST(test_telemetry_console_wifi_status_and_scan);
    RUN_TEST(test_telemetry_console_wifi_set_quoted_and_unquoted);
    RUN_TEST(test_telemetry_console_wifi_ap_forget_and_null_guard);

    return UNITY_END();
}
