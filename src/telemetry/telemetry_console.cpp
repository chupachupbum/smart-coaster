/**
 * @file telemetry_console.cpp
 * @brief Structured single-line JSON telemetry emitter and UART CLI implementation.
 */

#include "telemetry_console.h"
#include "net/network_push.h"
#include <cstdio>
#include <cstring>
#include <cctype>
#include <cstdlib>

TelemetryConsole::TelemetryConsole(uint32_t heartbeat_interval_ms)
    : rx_len_(0),
      rx_overflow_(false),
      last_was_cr_(false),
      last_telemetry_ms_(0),
      heartbeat_interval_ms_(heartbeat_interval_ms),
      wifi_status_("DISCONNECTED"),
      server_status_("UNKNOWN"),
      network_(nullptr) {
    resetRx();
}

void TelemetryConsole::begin() {
    last_telemetry_ms_ = 0;
    resetRx();
}

void TelemetryConsole::resetRx() {
    rx_len_ = 0;
    rx_overflow_ = false;
    last_was_cr_ = false;
    rx_buffer_[0] = '\0';
}

void TelemetryConsole::processUart(Stream& serial, CoasterDomain& domain, uint32_t now_ms) {
#ifdef ARDUINO
    if (now_ms == 0) {
        now_ms = millis();
    }
#endif

    size_t bytes_read = 0;
    while (serial.available() > 0 && bytes_read < MAX_READS_PER_TICK) {
        int c = serial.read();
        bytes_read++;
        if (c < 0) break;

        if (c == '\r') {
            last_was_cr_ = true;
            handleLineComplete(serial, domain, now_ms);
        } else if (c == '\n') {
            if (last_was_cr_) {
                // CRLF second byte: ignore to prevent duplicate command execution
                last_was_cr_ = false;
            } else {
                handleLineComplete(serial, domain, now_ms);
            }
        } else {
            last_was_cr_ = false;
            if (rx_len_ < RX_BUFFER_SIZE - 1) {
                rx_buffer_[rx_len_++] = static_cast<char>(c);
            } else {
                // Protect against line buffer overflow by dropping excess characters until newline
                rx_overflow_ = true;
            }
        }
    }
}

void TelemetryConsole::handleLineComplete(Stream& serial, CoasterDomain& domain, uint32_t now_ms) {
    rx_buffer_[rx_len_] = '\0';
    char tx_buf[TX_BUFFER_SIZE];

    executeCommand(rx_buffer_, domain, tx_buf, sizeof(tx_buf), now_ms);
    serial.println(tx_buf);

    rx_len_ = 0;
    rx_overflow_ = false;
}

size_t TelemetryConsole::executeCommand(const char* cmd_line, CoasterDomain& domain, char* out_buf, size_t max_out_len, uint32_t now_ms) {
    if (!cmd_line || !out_buf || max_out_len == 0) return 0;

    char clean_cmd[RX_BUFFER_SIZE];
    trimAndSanitize(cmd_line, clean_cmd, sizeof(clean_cmd));

    char cmd_upper[RX_BUFFER_SIZE];
    size_t len = strlen(clean_cmd);
    for (size_t i = 0; i <= len; ++i) {
        cmd_upper[i] = static_cast<char>(toupper(static_cast<unsigned char>(clean_cmd[i])));
    }

    // 1. Empty command
    if (len == 0) {
        return snprintf(out_buf, max_out_len,
            "{\"status\":\"ERROR\",\"command\":\"\",\"msg\":\"Supported: DEMO ON|OFF, TARE, RESET, GOAL <ml>, STATUS\",\"usage\":\"DEMO ON|OFF, TARE, RESET, GOAL <ml>, STATUS\"}");
    }

    // 2. DEMO ON
    if (strcmp(cmd_upper, "DEMO ON") == 0) {
        domain.set_demo_mode(true);
        return snprintf(out_buf, max_out_len,
            "{\"status\":\"OK\",\"command\":\"DEMO ON\",\"demo\":true,\"scale\":\"60x\"}");
    }

    // 3. DEMO OFF
    if (strcmp(cmd_upper, "DEMO OFF") == 0) {
        domain.set_demo_mode(false);
        return snprintf(out_buf, max_out_len,
            "{\"status\":\"OK\",\"command\":\"DEMO OFF\",\"demo\":false,\"scale\":\"1x\"}");
    }

    // 4. TARE
    if (strcmp(cmd_upper, "TARE") == 0) {
        domain.manual_tare();
        const CoasterStatus& st = domain.get_status();
        return snprintf(out_buf, max_out_len,
            "{\"status\":\"OK\",\"command\":\"TARE\",\"raw_offset\":%.1f,\"weight_g\":0.0}",
            st.tare_offset_g);
    }

    // 5. RESET
    if (strcmp(cmd_upper, "RESET") == 0) {
        domain.reset();
        const CoasterStatus& st = domain.get_status();
        return snprintf(out_buf, max_out_len,
            "{\"status\":\"OK\",\"command\":\"RESET\",\"consumed_ml\":0,\"goal_ml\":%.0f}",
            st.daily_goal_ml);
    }

    // 6. GOAL <ml>
    if (strncmp(cmd_upper, "GOAL", 4) == 0 && (cmd_upper[4] == ' ' || cmd_upper[4] == '\t' || cmd_upper[4] == '\0')) {
        const char* arg = clean_cmd + 4;
        while (*arg == ' ' || *arg == '\t') arg++;

        if (*arg == '\0') {
            return snprintf(out_buf, max_out_len,
                "{\"status\":\"ERROR\",\"command\":\"GOAL\",\"msg\":\"Invalid goal syntax. Expected: GOAL <ml>\"}");
        }

        char* endptr = nullptr;
        float val = strtof(arg, &endptr);
        if (endptr == arg) {
            return snprintf(out_buf, max_out_len,
                "{\"status\":\"ERROR\",\"command\":\"GOAL\",\"msg\":\"Invalid goal syntax. Expected: GOAL <ml>\"}");
        }
        while (*endptr == ' ' || *endptr == '\t') endptr++;
        if (*endptr != '\0') {
            return snprintf(out_buf, max_out_len,
                "{\"status\":\"ERROR\",\"command\":\"GOAL\",\"msg\":\"Invalid goal syntax. Expected: GOAL <ml>\"}");
        }

        if (val < 100.0f || val > 10000.0f) {
            return snprintf(out_buf, max_out_len,
                "{\"status\":\"ERROR\",\"command\":\"GOAL\",\"msg\":\"Goal must be between 100 and 10000 ml\"}");
        }

        domain.set_daily_goal(val);
        return snprintf(out_buf, max_out_len,
            "{\"status\":\"OK\",\"command\":\"GOAL\",\"goal_ml\":%.0f,\"progress_pct\":%.1f}",
            val, domain.get_progress_pct());
    }

    // 7. STATUS
    if (strcmp(cmd_upper, "STATUS") == 0) {
        const CoasterStatus& st = domain.get_status();
        return snprintf(out_buf, max_out_len,
            "{\"type\":\"response\",\"ts\":%lu,\"state\":\"%s\",\"weight_g\":%.1f,\"stable_g\":%.1f,"
            "\"stable\":%s,\"consumed_ml\":%.1f,\"goal_ml\":%.1f,\"progress_pct\":%.1f,"
            "\"countdown_s\":%lu,\"alert\":%s,\"demo\":%s,\"wifi\":\"%s\",\"server_status\":\"%s\",\"event\":\"STATUS\"}",
            (unsigned long)now_ms,
            CoasterDomain::cup_state_to_string(st.cup_state),
            st.net_weight_g,
            st.stable_weight_g,
            st.is_stable ? "true" : "false",
            st.consumed_today_ml,
            st.daily_goal_ml,
            st.progress_pct,
            (unsigned long)st.countdown_s,
            st.alert_active ? "true" : "false",
            st.demo_mode ? "true" : "false",
            wifi_status_,
            server_status_);
    }

    // 8. WIFI STATUS
    if (strcmp(cmd_upper, "WIFI STATUS") == 0) {
        if (!network_) {
            return snprintf(out_buf, max_out_len,
                "{\"status\":\"ERROR\",\"command\":\"WIFI\",\"msg\":\"Network manager unavailable\"}");
        }
        char ip_buf[20];
        network_->getIpString(ip_buf, sizeof(ip_buf));
        return snprintf(out_buf, max_out_len,
            "{\"status\":\"OK\",\"command\":\"WIFI STATUS\",\"wifi\":\"%s\",\"ssid\":\"%s\",\"ip\":\"%s\","
            "\"ap_active\":%s,\"server_status\":\"%s\",\"last_error\":\"%s\",\"failed_ssid\":\"%s\","
            "\"queue_size\":%lu,\"post_ok\":%lu,\"post_fail\":%lu}",
            network_->getWifiStateStr(),
            network_->getSsid(),
            ip_buf,
            network_->isApActive() ? "true" : "false",
            network_->getServerReachabilityStr(),
            network_->getLastErrorStr(),
            network_->getFailedSsid(),
            (unsigned long)network_->getQueueSize(),
            (unsigned long)network_->getSuccessfulPostCount(),
            (unsigned long)network_->getFailedPostCount());
    }

    // 9. WIFI SCAN
    if (strcmp(cmd_upper, "WIFI SCAN") == 0) {
        if (!network_) {
            return snprintf(out_buf, max_out_len,
                "{\"status\":\"ERROR\",\"command\":\"WIFI\",\"msg\":\"Network manager unavailable\"}");
        }
        network_->startAsyncScan(now_ms);
        size_t count = network_->getScanCount();
        const WifiScanEntry* entries = network_->getScanResults();
        int written = snprintf(out_buf, max_out_len,
            "{\"status\":\"OK\",\"command\":\"WIFI SCAN\",\"scanning\":%s,\"count\":%lu,\"networks\":[",
            network_->isScanning() ? "true" : "false",
            (unsigned long)count);
        if (written > 0 && (size_t)written < max_out_len) {
            size_t offset = (size_t)written;
            for (size_t i = 0; i < count; ++i) {
                char entry_buf[96];
                int elen = snprintf(entry_buf, sizeof(entry_buf),
                    "%s{\"ssid\":\"%s\",\"rssi\":%ld,\"channel\":%u,\"open\":%s}",
                    (i > 0 ? "," : ""),
                    entries[i].ssid,
                    (long)entries[i].rssi,
                    (unsigned int)entries[i].channel,
                    entries[i].auth_open ? "true" : "false");
                if (elen > 0 && offset + (size_t)elen + 3 < max_out_len) {
                    memcpy(out_buf + offset, entry_buf, elen);
                    offset += elen;
                } else {
                    break;
                }
            }
            if (offset + 2 < max_out_len) {
                out_buf[offset++] = ']';
                out_buf[offset++] = '}';
                out_buf[offset] = '\0';
                return offset;
            }
        }
        return (written > 0) ? (size_t)written : 0;
    }

    // 10. WIFI SET <ssid> [password]
    if (strncmp(cmd_upper, "WIFI SET", 8) == 0 && (cmd_upper[8] == ' ' || cmd_upper[8] == '\t' || cmd_upper[8] == '\0')) {
        if (!network_) {
            return snprintf(out_buf, max_out_len,
                "{\"status\":\"ERROR\",\"command\":\"WIFI\",\"msg\":\"Network manager unavailable\"}");
        }
        const char* p = clean_cmd + 8;
        while (*p == ' ' || *p == '\t') p++;

        if (*p == '\0') {
            return snprintf(out_buf, max_out_len,
                "{\"status\":\"ERROR\",\"command\":\"WIFI SET\",\"msg\":\"Missing SSID\"}");
        }

        char ssid_parsed[33];
        char pass_parsed[65];
        ssid_parsed[0] = '\0';
        pass_parsed[0] = '\0';

        if (*p == '"' || *p == '\'') {
            char q = *p++;
            size_t idx = 0;
            while (*p != '\0' && *p != q && idx < sizeof(ssid_parsed) - 1) {
                ssid_parsed[idx++] = *p++;
            }
            ssid_parsed[idx] = '\0';
            if (*p == q) p++;
        } else {
            size_t idx = 0;
            while (*p != '\0' && *p != ' ' && *p != '\t' && idx < sizeof(ssid_parsed) - 1) {
                ssid_parsed[idx++] = *p++;
            }
            ssid_parsed[idx] = '\0';
        }

        while (*p == ' ' || *p == '\t') p++;

        if (*p != '\0') {
            if (*p == '"' || *p == '\'') {
                char q = *p++;
                size_t idx = 0;
                while (*p != '\0' && *p != q && idx < sizeof(pass_parsed) - 1) {
                    pass_parsed[idx++] = *p++;
                }
                pass_parsed[idx] = '\0';
                if (*p == q) p++;
            } else {
                size_t idx = 0;
                while (*p != '\0' && *p != ' ' && *p != '\t' && idx < sizeof(pass_parsed) - 1) {
                    pass_parsed[idx++] = *p++;
                }
                pass_parsed[idx] = '\0';
            }
        }

        size_t s_len = strlen(ssid_parsed);
        size_t p_len = strlen(pass_parsed);
        if (s_len == 0 || s_len > 32 || p_len > 64) {
            return snprintf(out_buf, max_out_len,
                "{\"status\":\"ERROR\",\"command\":\"WIFI SET\",\"msg\":\"Invalid SSID or password length\"}");
        }

        network_->requestWifiSwitch(ssid_parsed, pass_parsed, now_ms);
        return snprintf(out_buf, max_out_len,
            "{\"status\":\"OK\",\"command\":\"WIFI SET\",\"ssid\":\"%s\",\"switching\":true}",
            ssid_parsed);
    }

    // 11. WIFI AP
    if (strcmp(cmd_upper, "WIFI AP") == 0) {
        if (!network_) {
            return snprintf(out_buf, max_out_len,
                "{\"status\":\"ERROR\",\"command\":\"WIFI\",\"msg\":\"Network manager unavailable\"}");
        }
        network_->startApFallback(now_ms);
        return snprintf(out_buf, max_out_len,
            "{\"status\":\"OK\",\"command\":\"WIFI AP\",\"ap_active\":true,\"ap_ssid\":\"%s\"}",
            WIFI_AP_SSID);
    }

    // 12. WIFI FORGET
    if (strcmp(cmd_upper, "WIFI FORGET") == 0) {
        if (!network_) {
            return snprintf(out_buf, max_out_len,
                "{\"status\":\"ERROR\",\"command\":\"WIFI\",\"msg\":\"Network manager unavailable\"}");
        }
        network_->forgetCredentials(now_ms);
        return snprintf(out_buf, max_out_len,
            "{\"status\":\"OK\",\"command\":\"WIFI FORGET\",\"ap_active\":true}");
    }

    // 13. Other WIFI subcommands
    if (strncmp(cmd_upper, "WIFI", 4) == 0 && (cmd_upper[4] == ' ' || cmd_upper[4] == '\t' || cmd_upper[4] == '\0')) {
        if (!network_) {
            return snprintf(out_buf, max_out_len,
                "{\"status\":\"ERROR\",\"command\":\"WIFI\",\"msg\":\"Network manager unavailable\"}");
        }
        return snprintf(out_buf, max_out_len,
            "{\"status\":\"ERROR\",\"command\":\"WIFI\",\"msg\":\"Supported: WIFI STATUS, WIFI SCAN, WIFI SET <ssid> [pass], WIFI AP, WIFI FORGET\"}");
    }

    // 14. Unknown command fallback
    return snprintf(out_buf, max_out_len,
        "{\"status\":\"ERROR\",\"command\":\"%s\",\"msg\":\"Supported: DEMO ON|OFF, TARE, RESET, GOAL <ml>, STATUS, WIFI\",\"usage\":\"DEMO ON|OFF, TARE, RESET, GOAL <ml>, STATUS, WIFI\"}",
        clean_cmd);
}

size_t TelemetryConsole::formatTelemetryJson(char* buffer, size_t max_len,
                                             const CoasterStatus& status,
                                             const char* event_name,
                                             uint32_t now_ms) {
    if (!buffer || max_len == 0) return 0;

    const char* cup_str = CoasterDomain::cup_state_to_string(status.cup_state);
    const char* alert_str = CoasterDomain::alert_state_to_string(status.alert_state);
    const char* evt = (event_name && event_name[0] != '\0') ? event_name : "HEARTBEAT";

    int written = snprintf(
        buffer, max_len,
        "{\"event\":\"%s\","
        "\"time_ms\":%lu,"
        "\"net_weight_g\":%.1f,"
        "\"cup_state\":\"%s\","
        "\"alert_state\":\"%s\","
        "\"is_stable\":%s,"
        "\"consumed_ml\":%.1f,"
        "\"daily_goal_ml\":%.1f,"
        "\"intake_pct\":%.1f,"
        "\"remind_countdown_s\":%lu,"
        "\"demo_mode\":%s,"
        "\"state\":\"%s\","
        "\"weight_g\":%.1f,"
        "\"goal_ml\":%.1f,"
        "\"countdown_s\":%lu,"
        "\"stable\":%s,"
        "\"demo\":%s}",
        evt,
        (unsigned long)now_ms,
        status.net_weight_g,
        cup_str,
        alert_str,
        status.is_stable ? "true" : "false",
        status.consumed_today_ml,
        status.daily_goal_ml,
        status.progress_pct,
        (unsigned long)status.countdown_s,
        status.demo_mode ? "true" : "false",
        cup_str,
        status.net_weight_g,
        status.daily_goal_ml,
        (unsigned long)status.countdown_s,
        status.is_stable ? "true" : "false",
        status.demo_mode ? "true" : "false"
    );

    if (written < 0 || (size_t)written >= max_len) {
        buffer[max_len - 1] = '\0';
        return 0;
    }
    return (size_t)written;
}

void TelemetryConsole::emitTelemetry(Stream& serial, const CoasterStatus& status, const char* event_name, uint32_t now_ms) {
    char buffer[TX_BUFFER_SIZE];
    size_t len = formatTelemetryJson(buffer, sizeof(buffer), status, event_name, now_ms);
    if (len > 0) {
        serial.println(buffer);
        last_telemetry_ms_ = now_ms;
    }
}

void TelemetryConsole::update(Stream& serial, const CoasterStatus& status, uint32_t now_ms) {
    if (now_ms - last_telemetry_ms_ >= heartbeat_interval_ms_) {
        emitTelemetry(serial, status, "HEARTBEAT", now_ms);
    }
}

void TelemetryConsole::update(Stream& serial, const CoasterDomain& domain, uint32_t now_ms) {
    update(serial, domain.get_status(), now_ms);
}

void TelemetryConsole::processEvents(Stream& serial, CoasterDomain& domain, uint32_t now_ms) {
    while (domain.has_events()) {
        DomainEvent ev = domain.pop_event();
        const char* evt_str = CoasterDomain::event_type_to_string(ev.type);
        emitTelemetry(serial, domain.get_status(), evt_str, now_ms);
    }
}

void TelemetryConsole::trimAndSanitize(const char* in, char* out, size_t out_max) {
    if (!in || !out || out_max == 0) return;
    while (*in == ' ' || *in == '\t' || *in == '\r' || *in == '\n') in++;

    size_t len = strlen(in);
    while (len > 0 && (in[len - 1] == ' ' || in[len - 1] == '\t' || in[len - 1] == '\r' || in[len - 1] == '\n')) {
        len--;
    }

    size_t copy_len = (len < out_max - 1) ? len : out_max - 1;
    for (size_t i = 0; i < copy_len; ++i) {
        char c = in[i];
        if (c == '"' || c == '\\') {
            out[i] = '\'';
        } else {
            out[i] = c;
        }
    }
    out[copy_len] = '\0';
}

