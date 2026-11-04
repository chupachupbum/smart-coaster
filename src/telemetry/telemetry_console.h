/**
 * @file telemetry_console.h
 * @brief Structured single-line JSON telemetry emitter and UART CLI interface.
 *
 * Implements:
 * - Single-line structured JSON telemetry emission with 11 mandated schema fields
 *   and 6 backward-compatible aliases for E2E assertions.
 * - Non-blocking UART CLI command parser supporting:
 *     - DEMO ON / DEMO OFF
 *     - TARE
 *     - RESET
 *     - GOAL <ml> (100 .. 10000 ml)
 *     - STATUS (full status dump snapshot)
 * - Safe line-buffering supporting CRLF (\r\n), LF (\n), and CR (\r) with overflow protection.
 * - Zero dynamic heap allocation: fixed static and stack buffers via snprintf.
 */

#ifndef TELEMETRY_TELEMETRY_CONSOLE_H_
#define TELEMETRY_TELEMETRY_CONSOLE_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef ARDUINO
#include <Arduino.h>
#include <Stream.h>
#else
#include <cstdio>
#include <cstring>
#include <cctype>

/**
 * @brief Minimal Stream abstraction enabling native host unit tests without Arduino framework.
 */
class Stream {
public:
    virtual ~Stream() = default;
    virtual int available() = 0;
    virtual int read() = 0;
    virtual size_t print(const char* s) = 0;
    virtual size_t println(const char* s) = 0;
};
#endif

#include "coaster_domain.h"

class TelemetryConsole {
public:
    static constexpr size_t RX_BUFFER_SIZE = 64;
    static constexpr size_t TX_BUFFER_SIZE = 512;
    static constexpr size_t MAX_READS_PER_TICK = 64;
    static constexpr uint32_t DEFAULT_HEARTBEAT_INTERVAL_MS = 5000;

    explicit TelemetryConsole(uint32_t heartbeat_interval_ms = DEFAULT_HEARTBEAT_INTERVAL_MS);

    /**
     * @brief Initialize telemetry timers and RX buffers.
     */
    void begin();

    /**
     * @brief Poll UART stream and process any completed command lines.
     * Non-blocking: reads up to MAX_READS_PER_TICK available characters.
     *
     * @param serial Reference to communication Stream (e.g. Serial).
     * @param domain Reference to CoasterDomain state machine.
     * @param now_ms Current system timestamp in milliseconds (defaults to 0 / millis()).
     */
    void processUart(Stream& serial, CoasterDomain& domain, uint32_t now_ms = 0);

    /**
     * @brief Parse and execute a single command line string, producing a structured JSON response.
     * Decoupled from Stream for host unit testability.
     *
     * @param cmd_line Input ASCII command line string.
     * @param domain Reference to CoasterDomain state machine.
     * @param out_buf Destination buffer for JSON response.
     * @param max_out_len Size of destination buffer.
     * @param now_ms Current system timestamp in milliseconds.
     * @return Number of characters written to out_buf (excluding null terminator).
     */
    size_t executeCommand(const char* cmd_line, CoasterDomain& domain, char* out_buf, size_t max_out_len, uint32_t now_ms = 0);

    /**
     * @brief Emit single-line structured JSON telemetry event packet.
     * Formats all 11 mandated schema fields plus compatibility aliases.
     * Resets heartbeat timer on emission.
     *
     * @param serial Destination Stream.
     * @param status Current snapshot from CoasterDomain.
     * @param event_name Event label (e.g. "DRINK_LOGGED", "REMINDER_ALERT", "HEARTBEAT").
     * @param now_ms System timestamp in milliseconds.
     */
    void emitTelemetry(Stream& serial, const CoasterStatus& status, const char* event_name, uint32_t now_ms);

    /**
     * @brief Core pure JSON serialization into a caller-provided buffer.
     * 100% zero heap allocations, fully testable on native host.
     *
     * @param buffer Output character buffer.
     * @param max_len Capacity of output buffer.
     * @param status Snapshot of domain status metrics.
     * @param event_name Name of the triggering event.
     * @param now_ms Current timestamp.
     * @return Number of characters written (excluding null terminator), or 0 on error/truncation.
     */
    static size_t formatTelemetryJson(char* buffer, size_t max_len,
                                      const CoasterStatus& status,
                                      const char* event_name,
                                      uint32_t now_ms);

    /**
     * @brief Periodic update function to emit heartbeat telemetry if interval elapsed.
     */
    void update(Stream& serial, const CoasterStatus& status, uint32_t now_ms);

    /**
     * @brief Overload accepting CoasterDomain directly.
     */
    void update(Stream& serial, const CoasterDomain& domain, uint32_t now_ms);

    /**
     * @brief Drain queued domain events from CoasterDomain and emit telemetry for each.
     */
    void processEvents(Stream& serial, CoasterDomain& domain, uint32_t now_ms);

    /**
     * @brief Reset RX line buffer and state.
     */
    void resetRx();

    /**
     * @brief Configure periodic telemetry heartbeat interval.
     */
    void setHeartbeatInterval(uint32_t interval_ms) { heartbeat_interval_ms_ = interval_ms; }
    uint32_t getHeartbeatInterval() const { return heartbeat_interval_ms_; }
    uint32_t getLastTelemetryTimeMs() const { return last_telemetry_ms_; }

    /**
     * @brief Configure Wi-Fi status string reported in telemetry/status dumps.
     */
    void setWifiStatus(const char* status_str) { wifi_status_ = status_str ? status_str : "DISCONNECTED"; }
    const char* getWifiStatus() const { return wifi_status_; }

private:
    char rx_buffer_[RX_BUFFER_SIZE];
    size_t rx_len_ = 0;
    bool rx_overflow_ = false;
    bool last_was_cr_ = false;

    uint32_t last_telemetry_ms_ = 0;
    uint32_t heartbeat_interval_ms_ = DEFAULT_HEARTBEAT_INTERVAL_MS;
    const char* wifi_status_ = "DISCONNECTED";

    void handleLineComplete(Stream& serial, CoasterDomain& domain, uint32_t now_ms);
    static void trimAndSanitize(const char* in, char* out, size_t out_max);
};

#endif // TELEMETRY_TELEMETRY_CONSOLE_H_

