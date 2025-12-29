// LXMF Messenger for LilyGO T-Deck Plus
// Complete LXMF messaging application with LVGL UI

#include <Arduino.h>
#include <Wire.h>
#include <SPIFFS.h>
#include <Preferences.h>
#include <time.h>
#include <sys/time.h>

// Reticulum
#include <Reticulum.h>
#include <Utilities/OS.h>

// Filesystem
#include <UniversalFileSystem.h>
#include <Identity.h>
#include <Destination.h>
#include <Transport.h>
#include <Interface.h>

// TCP Client Interface
#include "TCPClientInterface.h"

// LXMF
#include <LXMF/LXMRouter.h>
#include <LXMF/MessageStore.h>

// Hardware drivers
#include <Hardware/TDeck/Config.h>
#include <Hardware/TDeck/Display.h>
#include <Hardware/TDeck/Keyboard.h>
#include <Hardware/TDeck/Touch.h>
#include <Hardware/TDeck/Trackball.h>

// GPS
#include <TinyGPSPlus.h>

// UI
#include <UI/LVGL/LVGLInit.h>
#include <UI/LXMF/UIManager.h>
#include <UI/LXMF/SettingsScreen.h>

// Logging
#include <Log.h>

using namespace RNS;
using namespace LXMF;
using namespace Hardware::TDeck;

// Application settings (loaded from NVS)
UI::LXMF::AppSettings app_settings;

// Global instances
Reticulum* reticulum = nullptr;
Identity* identity = nullptr;
LXMRouter* router = nullptr;
MessageStore* message_store = nullptr;
UI::LXMF::UIManager* ui_manager = nullptr;
TCPClientInterface* tcp_interface_impl = nullptr;
Interface* tcp_interface = nullptr;

// Timing
uint32_t last_ui_update = 0;
uint32_t last_announce = 0;
uint32_t last_status_check = 0;
const uint32_t STATUS_CHECK_INTERVAL = 1000;  // 1 second

// Connection tracking
bool last_rns_online = false;

// Screen timeout
bool screen_off = false;
uint8_t saved_brightness = 180;  // Save brightness before turning off

// GPS
TinyGPSPlus gps;
HardwareSerial GPSSerial(1);  // UART1 for GPS
bool gps_time_synced = false;

/**
 * Calculate timezone offset from longitude
 * Each 15 degrees of longitude = 1 hour offset from UTC
 * Positive = East of Greenwich (ahead of UTC)
 * Negative = West of Greenwich (behind UTC)
 */
int calculate_timezone_offset_hours(double longitude) {
    // Simple calculation: divide longitude by 15, round to nearest hour
    int offset = (int)round(longitude / 15.0);
    // Clamp to valid range (-12 to +14)
    if (offset < -12) offset = -12;
    if (offset > 14) offset = 14;
    return offset;
}

/**
 * Try to sync time from GPS
 * Returns true if successful, false if no valid fix
 */
bool sync_time_from_gps(uint32_t timeout_ms = 30000) {
    INFO("Attempting GPS time sync...");

    uint32_t start = millis();
    bool got_time = false;
    bool got_location = false;

    while (millis() - start < timeout_ms) {
        while (GPSSerial.available() > 0) {
            if (gps.encode(GPSSerial.read())) {
                // Check if we have valid date/time
                if (gps.date.isValid() && gps.time.isValid() && gps.date.year() >= 2024) {
                    got_time = true;
                }
                // Check if we have valid location (for timezone)
                if (gps.location.isValid()) {
                    got_location = true;
                }
                // If we have both, we can sync
                if (got_time && got_location) {
                    break;
                }
            }
        }
        if (got_time && got_location) break;
        delay(10);
    }

    if (!got_time) {
        WARNING("GPS time not available");
        return false;
    }

    // Build UTC time from GPS
    struct tm gps_time;
    gps_time.tm_year = gps.date.year() - 1900;
    gps_time.tm_mon = gps.date.month() - 1;
    gps_time.tm_mday = gps.date.day();
    gps_time.tm_hour = gps.time.hour();
    gps_time.tm_min = gps.time.minute();
    gps_time.tm_sec = gps.time.second();
    gps_time.tm_isdst = 0;  // GPS time is UTC, no DST

    // Convert to Unix timestamp (UTC)
    time_t gps_unix = mktime(&gps_time);
    // mktime assumes local time, adjust back to UTC
    // Actually, we'll set TZ to UTC first
    setenv("TZ", "UTC0", 1);
    tzset();
    gps_unix = mktime(&gps_time);

    // Set the system time
    struct timeval tv;
    tv.tv_sec = gps_unix;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);

    // Set timezone based on location if available
    if (got_location) {
        double longitude = gps.location.lng();
        int tz_offset = calculate_timezone_offset_hours(longitude);

        // Build POSIX TZ string (e.g., "EST5" for UTC-5)
        // Note: POSIX uses opposite sign convention!
        char tz_str[32];
        if (tz_offset >= 0) {
            snprintf(tz_str, sizeof(tz_str), "GPS%d", -tz_offset);
        } else {
            snprintf(tz_str, sizeof(tz_str), "GPS+%d", -tz_offset);
        }
        setenv("TZ", tz_str, 1);
        tzset();

        String msg = "  GPS location: " + String(gps.location.lat(), 4) + ", " + String(longitude, 4);
        INFO(msg.c_str());
        msg = "  Timezone offset: UTC" + String(tz_offset >= 0 ? "+" : "") + String(tz_offset);
        INFO(msg.c_str());
    } else {
        // No location, use default Eastern Time
        WARNING("GPS location not available, using Eastern Time");
        setenv("TZ", "EST5EDT,M3.2.0,M11.1.0", 1);
        tzset();
    }

    // Set the time offset for Utilities::OS::time()
    time_t now = time(nullptr);
    uint64_t uptime_ms = millis();
    uint64_t unix_ms = (uint64_t)now * 1000;
    RNS::Utilities::OS::setTimeOffset(unix_ms - uptime_ms);

    // Display synced time
    struct tm timeinfo;
    getLocalTime(&timeinfo);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S %Z", &timeinfo);
    String msg = "  GPS time synced: " + String(time_str);
    INFO(msg.c_str());

    gps_time_synced = true;
    return true;
}

void setup_gps() {
    INFO("Initializing GPS...");

    // Initialize GPS serial (9600 baud is default for most GPS modules)
    GPSSerial.begin(9600, SERIAL_8N1, Pin::GPS_TX, Pin::GPS_RX);

    String gps_msg = "  GPS UART initialized on TX=" + String(Pin::GPS_TX) + ", RX=" + String(Pin::GPS_RX);
    INFO(gps_msg.c_str());
}

void load_app_settings() {
    INFO("Loading application settings from NVS...");

    Preferences prefs;
    prefs.begin("settings", true);  // Read-only

    // Network
    app_settings.wifi_ssid = prefs.getString("wifi_ssid", "");
    app_settings.wifi_password = prefs.getString("wifi_pass", "");
    app_settings.tcp_host = prefs.getString("tcp_host", "YOUR_SERVER_IP");
    app_settings.tcp_port = prefs.getUShort("tcp_port", 4965);

    // Identity
    app_settings.display_name = prefs.getString("disp_name", "");

    // Display
    app_settings.brightness = prefs.getUChar("brightness", 180);
    app_settings.screen_timeout = prefs.getUShort("timeout", 60);

    // Advanced
    app_settings.announce_interval = prefs.getULong("announce", 60);
    app_settings.gps_time_sync = prefs.getBool("gps_sync", true);

    prefs.end();

    // Log loaded settings (hide password)
    String msg = "  WiFi SSID: " + (app_settings.wifi_ssid.length() > 0 ? app_settings.wifi_ssid : "(not set)");
    INFO(msg.c_str());
    msg = "  TCP Server: " + app_settings.tcp_host + ":" + String(app_settings.tcp_port);
    INFO(msg.c_str());
    msg = "  Brightness: " + String(app_settings.brightness);
    INFO(msg.c_str());
}

void setup_wifi() {
    // Check if WiFi credentials are configured
    if (app_settings.wifi_ssid.length() == 0) {
        WARNING("WiFi not configured - skipping WiFi setup");
        return;
    }

    String msg = "Connecting to WiFi: " + app_settings.wifi_ssid;
    INFO(msg.c_str());

    WiFi.mode(WIFI_STA);
    WiFi.begin(app_settings.wifi_ssid.c_str(), app_settings.wifi_password.c_str());

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 30000) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        INFO("WiFi connected!");
        msg = "  IP address: " + WiFi.localIP().toString();
        INFO(msg.c_str());
        msg = "  RSSI: " + String(WiFi.RSSI()) + " dBm";
        INFO(msg.c_str());

        // Try GPS time sync first (if GPS is initialized and we haven't synced already)
        if (!gps_time_synced) {
            // Fallback to NTP if GPS didn't work
            INFO("Syncing time via NTP (GPS not available)...");

            // Use configTzTime for proper timezone handling on ESP32
            // Eastern Time: EST5EDT = UTC-5, DST starts 2nd Sunday March, ends 1st Sunday Nov
            configTzTime("EST5EDT,M3.2.0,M11.1.0", "pool.ntp.org", "time.nist.gov");

            // Wait for time to be set (max 10 seconds)
            struct tm timeinfo;
            int retry = 0;
            while (!getLocalTime(&timeinfo) && retry < 20) {
                delay(500);
                retry++;
            }

            if (retry < 20) {
                // Set the time offset for Utilities::OS::time()
                time_t now = time(nullptr);
                uint64_t uptime_ms = millis();
                uint64_t unix_ms = (uint64_t)now * 1000;
                RNS::Utilities::OS::setTimeOffset(unix_ms - uptime_ms);

                char time_str[64];
                strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S %Z", &timeinfo);
                msg = "  NTP time synced: " + String(time_str);
                INFO(msg.c_str());
            } else {
                WARNING("NTP time sync failed!");
            }
        } else {
            INFO("Time already synced via GPS");
        }
    } else {
        ERROR("WiFi connection failed!");
    }
}

void setup_hardware() {
    INFO("\n=== Hardware Initialization ===");

    // Initialize SPIFFS for persistence via UniversalFileSystem
    // NOTE: Do NOT call SPIFFS.begin() here - UniversalFileSystem::init() handles it
    static RNS::FileSystem fs = new UniversalFileSystem();
    if (!fs.init()) {
        ERROR("FileSystem mount failed!");
    } else {
        INFO("FileSystem mounted");
        RNS::Utilities::OS::register_filesystem(fs);
        INFO("Filesystem registered");
    }

    // Initialize I2C for keyboard and touch
    Wire.begin(Pin::I2C_SDA, Pin::I2C_SCL);
    Wire.setClock(I2C::FREQUENCY);
    INFO("I2C initialized");

    // Initialize power
    pinMode(Pin::POWER_EN, OUTPUT);
    digitalWrite(Pin::POWER_EN, HIGH);
    INFO("Power enabled");
}

void setup_lvgl_and_ui() {
    INFO("\n=== LVGL & UI Initialization ===");

    // Initialize LVGL with all hardware drivers
    if (!UI::LVGL::LVGLInit::init()) {
        ERROR("LVGL initialization failed!");
        while (1) delay(1000);
    }

    INFO("LVGL initialized");
}

void setup_reticulum() {
    INFO("\n=== Reticulum Initialization ===");

    // Create Reticulum instance (no auto-init)
    reticulum = new Reticulum();

    // Load or create identity using NVS (Non-Volatile Storage)
    // NVS is preserved across flashes unlike SPIFFS
    Preferences prefs;
    prefs.begin("reticulum", false);  // namespace "reticulum", read-write

    size_t key_len = prefs.getBytesLength("identity");
    INFO("Checking for identity in NVS...");
    Serial.printf("NVS identity key length: %u\n", key_len);

    if (key_len == 64) {  // Private key is 64 bytes
        INFO("Identity found in NVS, loading...");
        uint8_t key_data[64];
        prefs.getBytes("identity", key_data, 64);
        Bytes private_key(key_data, 64);
        identity = new Identity(false);  // Create without generating keys
        if (identity->load_private_key(private_key)) {
            INFO("  Identity loaded successfully from NVS");
        } else {
            ERROR("  Failed to load identity from NVS, creating new");
            identity = new Identity();
            Bytes priv_key = identity->get_private_key();
            prefs.putBytes("identity", priv_key.data(), priv_key.size());
            INFO("  New identity saved to NVS");
        }
    } else {
        INFO("No identity in NVS, creating new identity");
        identity = new Identity();
        Bytes priv_key = identity->get_private_key();
        size_t written = prefs.putBytes("identity", priv_key.data(), priv_key.size());
        Serial.printf("  Wrote %u bytes to NVS\n", written);
        INFO("  Identity saved to NVS");
    }
    prefs.end();

    std::string identity_hex = identity->get_public_key().toHex().substr(0, 16);
    std::string msg = "  Identity: " + identity_hex + "...";
    INFO(msg.c_str());

    // Add TCP client interface (only if WiFi is connected)
    if (WiFi.status() == WL_CONNECTED) {
        String server_addr = app_settings.tcp_host + ":" + String(app_settings.tcp_port);
        msg = std::string("Connecting to RNS server at ") + server_addr.c_str();
        INFO(msg.c_str());

        tcp_interface_impl = new TCPClientInterface("tcp0");
        tcp_interface_impl->set_target_host(app_settings.tcp_host.c_str());
        tcp_interface_impl->set_target_port(app_settings.tcp_port);
        tcp_interface = new Interface(tcp_interface_impl);

        if (!tcp_interface->start()) {
            ERROR("Failed to connect to RNS server!");
        } else {
            INFO("Connected to RNS server");
            Transport::register_interface(*tcp_interface);
        }
    } else {
        WARNING("WiFi not connected - skipping TCP interface");
    }

    // Start Transport (initializes Transport identity and enables packet processing)
    reticulum->start();
}

void setup_lxmf() {
    INFO("\n=== LXMF Initialization ===");

    // Create message store
    message_store = new MessageStore("/lxmf");
    INFO("Message store ready");

    // Create LXMF router
    router = new LXMRouter(*identity, "/lxmf");
    INFO("LXMF router created");

    // Set display name from settings for announces
    if (!app_settings.display_name.isEmpty()) {
        router->set_display_name(app_settings.display_name.c_str());
    }

    // Only do network stuff if TCP interface exists
    if (tcp_interface) {
        // Wait for TCP connection to stabilize before announcing
        INFO("Waiting 3 seconds for TCP connection to stabilize...");
        delay(3000);

        // Check TCP status before announcing
        if (tcp_interface->online()) {
            INFO("TCP interface online: YES");
            // Announce delivery destination
            INFO("Sending LXMF announce...");
            router->announce();
            last_announce = millis();
        } else {
            INFO("TCP interface online: NO");
        }
    } else {
        WARNING("No TCP interface - network features disabled until WiFi configured");
    }

    std::string dest_hash = router->delivery_destination().hash().toHex();
    std::string msg = "  Delivery destination: " + dest_hash;
    INFO(msg.c_str());
}

void setup_ui_manager() {
    INFO("\n=== UI Manager Initialization ===");

    // Create UI manager
    ui_manager = new UI::LXMF::UIManager(*reticulum, *router, *message_store);

    if (!ui_manager->init()) {
        ERROR("UI manager initialization failed!");
        while (1) delay(1000);
    }

    // Set initial RNS connection status
    if (tcp_interface) {
        ui_manager->set_rns_status(tcp_interface->online(), app_settings.tcp_host);
    }

    // Configure settings screen
    UI::LXMF::SettingsScreen* settings = ui_manager->get_settings_screen();
    if (settings) {
        // Pass GPS for status display
        settings->set_gps(&gps);

        // Set brightness change callback (immediate)
        settings->set_brightness_change_callback([](uint8_t brightness) {
            // Apply brightness immediately via display backlight
            ledcWrite(0, brightness);  // Channel 0 is backlight on T-Deck
            INFO(("Brightness changed to " + String(brightness)).c_str());
        });

        // Set WiFi reconnect callback
        settings->set_wifi_reconnect_callback([](const String& ssid, const String& password) {
            INFO(("Reconnecting WiFi to: " + ssid).c_str());
            WiFi.disconnect();
            delay(100);
            WiFi.begin(ssid.c_str(), password.c_str());

            // Wait for connection (with timeout)
            uint32_t start = millis();
            while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
                delay(100);
            }

            if (WiFi.status() == WL_CONNECTED) {
                INFO(("WiFi connected! IP: " + WiFi.localIP().toString()).c_str());
            } else {
                WARNING("WiFi reconnection failed");
            }
        });

        // Set save callback (update app_settings and apply)
        settings->set_save_callback([](const UI::LXMF::AppSettings& new_settings) {
            app_settings = new_settings;
            // Update router display name
            if (router && !new_settings.display_name.isEmpty()) {
                router->set_display_name(new_settings.display_name.c_str());
            }
            INFO("Settings saved");
        });
    }

    // Apply initial brightness from settings
    ledcWrite(0, app_settings.brightness);

    INFO("UI manager ready");
}

void setup() {
    // Initialize serial
    Serial.begin(115200);
    delay(2000);

    // Wait for serial connection with countdown
    Serial.println("\n\n=== Waiting 5 seconds for serial monitor ===");
    for (int i = 5; i > 0; i--) {
        Serial.print(i);
        Serial.println("...");
        delay(1000);
    }
    Serial.println("Starting...");

    INFO("\n");
    INFO("╔══════════════════════════════════════╗");
    INFO("║   LXMF Messenger for T-Deck Plus    ║");
    INFO("║   microReticulum + LVGL UI          ║");
    INFO("╚══════════════════════════════════════╝");
    INFO("");

    // Initialize hardware
    setup_hardware();

    // Load application settings from NVS (before WiFi/GPS)
    load_app_settings();

    // Initialize GPS and try to sync time (before WiFi)
    setup_gps();
    if (app_settings.gps_time_sync) {
        INFO("\n=== Time Synchronization ===");
        if (!sync_time_from_gps(15000)) {  // 15 second timeout for GPS
            INFO("GPS time sync not available, will try NTP after WiFi");
        }
    } else {
        INFO("GPS time sync disabled in settings");
    }

    // Initialize WiFi
    setup_wifi();

    // Initialize LVGL and hardware drivers
    setup_lvgl_and_ui();

    // Initialize Reticulum
    setup_reticulum();

    // Initialize LXMF
    setup_lxmf();

    // Initialize UI manager
    setup_ui_manager();

    INFO("\n");
    INFO("╔══════════════════════════════════════╗");
    INFO("║     System Ready - Enjoy!            ║");
    INFO("╚══════════════════════════════════════╝");
    INFO("");

    // Show startup message
    INFO("Press any key to start messaging");
}

void loop() {
    // Handle LVGL rendering (must be called frequently for smooth UI)
    UI::LVGL::LVGLInit::task_handler();

    // Process Reticulum
    reticulum->loop();

    // Process TCP interface
    if (tcp_interface) {
        tcp_interface->loop();
    }

    // Update UI manager (processes LXMF messages)
    if (ui_manager) {
        ui_manager->update();
    }

    // Periodic announce (using interval from settings)
    uint32_t announce_interval_ms = app_settings.announce_interval * 1000;
    if (millis() - last_announce > announce_interval_ms) {
        if (router) {
            router->announce();
            last_announce = millis();
            TRACE("Periodic announce sent");
        }
    }

    // Check for reconnection (handles rapid disconnect/reconnect that status check might miss)
    if (tcp_interface_impl && tcp_interface_impl->check_reconnected()) {
        INFO("TCP interface reconnected - sending announce");
        if (router) {
            delay(500);  // Brief stabilization delay
            router->announce();
            last_announce = millis();
        }
        // Update UI status
        if (ui_manager) {
            ui_manager->set_rns_status(true, app_settings.tcp_host);
        }
        last_rns_online = true;
    }

    // Periodic RNS status check (backup for slower state changes)
    if (millis() - last_status_check > STATUS_CHECK_INTERVAL) {
        last_status_check = millis();
        if (tcp_interface && ui_manager) {
            bool current_online = tcp_interface->online();
            if (current_online != last_rns_online) {
                last_rns_online = current_online;
                ui_manager->set_rns_status(current_online, app_settings.tcp_host);
                if (!current_online) {
                    WARNING("RNS connection lost");
                }
            }
        }
    }

    // Screen timeout handling
    if (app_settings.screen_timeout > 0) {  // 0 = never timeout
        uint32_t inactive_ms = lv_disp_get_inactive_time(NULL);
        uint32_t timeout_ms = app_settings.screen_timeout * 1000;

        if (!screen_off && inactive_ms > timeout_ms) {
            // Screen has been inactive long enough - turn off backlight
            saved_brightness = app_settings.brightness;
            ledcWrite(0, 0);  // Turn off backlight (channel 0)
            screen_off = true;
            DEBUG("Screen timeout - backlight off");
        }
        else if (screen_off && inactive_ms < 1000) {
            // Activity detected - turn screen back on
            ledcWrite(0, saved_brightness);
            screen_off = false;
            DEBUG("Activity detected - backlight on");
        }
    }

    // Periodic heap monitoring (every 10 seconds)
    static uint32_t last_heap_check = 0;
    if (millis() - last_heap_check > 10000) {
        last_heap_check = millis();
        Serial.printf("[HEAP] free=%u min=%u\n",
            ESP.getFreeHeap(), ESP.getMinFreeHeap());
    }

    // Small delay to prevent tight loop
    delay(5);
}
