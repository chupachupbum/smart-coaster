#ifndef NET_WIFI_PORTAL_H_
#define NET_WIFI_PORTAL_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef ARDUINO
#include <Arduino.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#endif

#include "config.h"
#include "coaster_domain.h"
#include "net/network_push.h"
#include "hal/led_ring.h"

class WifiPortal {
public:
    WifiPortal(CoasterDomain& domain, NetworkPush& network, LedRing& led_ring);
    ~WifiPortal() = default;

    void begin();
    void update(uint32_t now_ms);

    int handleRequest(const char* method, const char* path, const char* body,
                      char* out_buf, size_t max_out_len, uint32_t now_ms,
                      const char** out_content_type = nullptr);

    static const char* getIndexHtml();

private:
    CoasterDomain& domain_;
    NetworkPush&   network_;
    LedRing&       led_ring_;

#ifdef ARDUINO
    WebServer server_;
    DNSServer dns_server_;
    bool dns_started_ = false;
    bool mdns_started_ = false;
    uint32_t last_connected_ip_ = 0;
#endif
};

#endif // NET_WIFI_PORTAL_H_
