#ifndef CONFIG_H_
#define CONFIG_H_

#include <stdint.h>

// Optional local credentials override (gitignored; copy include/secrets.example.h -> include/secrets.h)
#if defined(__has_include)
#if __has_include("secrets.h")
#include "secrets.h"
#endif
#endif

// ============================================================================
// Smart Cup Stand - System & Network Configuration
// ============================================================================

// ----------------------------------------------------------------------------
// 1. Wi-Fi Configuration
// ----------------------------------------------------------------------------
// Wokwi simulation uses the open "Wokwi-GUEST" access point.
// In real-world deployment, override via build_flags or custom defines.
#ifndef WIFI_SSID
#define WIFI_SSID                   "Wokwi-GUEST"
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD               ""
#endif

// Non-blocking connection timeout: abort connection attempt after 10 seconds
#ifndef WIFI_CONNECT_TIMEOUT_MS
#define WIFI_CONNECT_TIMEOUT_MS     10000UL
#endif

// Initial retry backoff interval when disconnected or failed (15 seconds)
#ifndef WIFI_RETRY_INTERVAL_MS
#define WIFI_RETRY_INTERVAL_MS      15000UL
#endif

// Maximum exponential backoff interval (60 seconds)
#ifndef WIFI_MAX_RETRY_BACKOFF_MS
#define WIFI_MAX_RETRY_BACKOFF_MS   60000UL
#endif

// ----------------------------------------------------------------------------
// 2. ntfy.sh HTTP Push Notification Configuration
// ----------------------------------------------------------------------------
// Target server URL. Plain HTTP is preferred in Wokwi and low-overhead ESP32
// to eliminate TLS certificate parsing and RAM allocation overhead.
#ifndef NTFY_SERVER_URL
#define NTFY_SERVER_URL             "http://ntfy.sh"
#endif

// Default public/private topic name on ntfy.sh
#ifndef NTFY_DEFAULT_TOPIC
#define NTFY_DEFAULT_TOPIC          "smart-cup-stand"
#endif

// HTTP client socket connect & read timeout (3000 ms)
#ifndef NTFY_HTTP_TIMEOUT_MS
#define NTFY_HTTP_TIMEOUT_MS        3000UL
#endif

// Minimum throttle interval between consecutive HTTP POST requests (2000 ms)
// Prevents flooding ntfy.sh public rate limiters.
#ifndef NTFY_MIN_POST_INTERVAL_MS
#define NTFY_MIN_POST_INTERVAL_MS   2000UL
#endif

// Notification queue bounded capacity (maxlen 3, drops oldest on overflow)
// Certified invariant matching E2E test suite.
#ifndef NTFY_MAX_QUEUE_CAPACITY
#define NTFY_MAX_QUEUE_CAPACITY     3
#endif

// ----------------------------------------------------------------------------
// 3. Notification Payloads & Headers (E2E-Certified Schema)
// ----------------------------------------------------------------------------
// Drink Logged Notification
#ifndef NTFY_TITLE_DRINK_LOGGED
#define NTFY_TITLE_DRINK_LOGGED     "Drink Logged - Smart Coaster"
#endif
#ifndef NTFY_PRIORITY_DRINK_LOGGED
#define NTFY_PRIORITY_DRINK_LOGGED  3
#endif
#ifndef NTFY_TAGS_DRINK_LOGGED
#define NTFY_TAGS_DRINK_LOGGED      "glass_of_milk,droplet"
#endif

// Hydration Reminder Alert Notification (Primary Inactivity Alert)
#ifndef NTFY_TITLE_REMINDER
#define NTFY_TITLE_REMINDER         "Hydration Alert"
#endif
#ifndef NTFY_MESSAGE_REMINDER
#define NTFY_MESSAGE_REMINDER       "Time to drink! You haven't taken a drink in 45 minutes."
#endif
#ifndef NTFY_PRIORITY_REMINDER
#define NTFY_PRIORITY_REMINDER      4
#endif
#ifndef NTFY_TAGS_REMINDER
#define NTFY_TAGS_REMINDER          "alarm_clock,warning"
#endif

// Repeating Hydration Alert Notification
#ifndef NTFY_TITLE_REPEAT
#define NTFY_TITLE_REPEAT           "Hydration Alert (Repeat)"
#endif
#ifndef NTFY_MESSAGE_REPEAT
#define NTFY_MESSAGE_REPEAT         "Hydration reminder: Still waiting for you to drink water!"
#endif
#ifndef NTFY_PRIORITY_REPEAT
#define NTFY_PRIORITY_REPEAT        4
#endif
#ifndef NTFY_TAGS_REPEAT
#define NTFY_TAGS_REPEAT            "repeat,warning"
#endif

// Daily Goal Achieved Notification
#ifndef NTFY_TITLE_GOAL_ACHIEVED
#define NTFY_TITLE_GOAL_ACHIEVED    "Daily Goal Achieved!"
#endif
#ifndef NTFY_PRIORITY_GOAL_ACHIEVED
#define NTFY_PRIORITY_GOAL_ACHIEVED 3
#endif
#ifndef NTFY_TAGS_GOAL_ACHIEVED
#define NTFY_TAGS_GOAL_ACHIEVED     "tada,trophy"
#endif

// ----------------------------------------------------------------------------
// 4. System & Domain Defaults
// ----------------------------------------------------------------------------
#ifndef DEFAULT_DAILY_GOAL_ML
#define DEFAULT_DAILY_GOAL_ML       2000.0f
#endif

#ifndef TELEMETRY_HEARTBEAT_INTERVAL_MS
#define TELEMETRY_HEARTBEAT_INTERVAL_MS 5000UL
#endif

#endif // CONFIG_H_

