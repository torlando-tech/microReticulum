/*
 * Display.cpp - OLED display implementation for microReticulum
 */

#include "Display.h"

#ifdef HAS_DISPLAY

#include "Identity.h"
#include "Interface.h"
#include "Reticulum.h"
#include "Transport.h"
#include "Log.h"
#include "Utilities/OS.h"

// Display library includes (board-specific)
#ifdef ARDUINO
    #include <Wire.h>
    #ifdef DISPLAY_TYPE_SH1106
        #include <Adafruit_SH110X.h>
    #elif defined(DISPLAY_TYPE_SSD1306)
        #include <Adafruit_SSD1306.h>
    #endif
    #include <Adafruit_GFX.h>
#endif

namespace RNS {

// Display dimensions
static const int16_t DISPLAY_WIDTH = 128;
static const int16_t DISPLAY_HEIGHT = 64;

// Layout constants
static const int16_t HEADER_HEIGHT = 17;
static const int16_t CONTENT_Y = 20;
static const int16_t LINE_HEIGHT = 11;
static const int16_t LEFT_MARGIN = 2;

// Static member initialization
bool Display::_ready = false;
bool Display::_blanked = false;
uint32_t Display::_last_update = 0;
uint32_t Display::_start_time = 0;
uint32_t Display::_frame_count = 0;
Bytes Display::_identity_hash;
std::vector<Interface*> Display::_interfaces;
Reticulum* Display::_reticulum = nullptr;
float Display::_rssi = -120.0f;
size_t Display::_ble_central_peers = 0;
size_t Display::_ble_peripheral_peers = 0;
size_t Display::_auto_peers = 0;

// Display object (board-specific)
#ifdef ARDUINO
    #ifdef DISPLAY_TYPE_SH1106
        static Adafruit_SH1106G* display = nullptr;
    #elif defined(DISPLAY_TYPE_SSD1306)
        static Adafruit_SSD1306* display = nullptr;
    #endif
#endif

bool Display::init() {
#ifdef ARDUINO
    TRACE("Display::init: Initializing display...");

    // Initialize I2C
    #if defined(DISPLAY_SDA) && defined(DISPLAY_SCL)
        Wire.begin(DISPLAY_SDA, DISPLAY_SCL);
    #else
        Wire.begin();
    #endif
    delay(100);  // Let I2C bus stabilize (required for reliable init)

    // Create display object
    #ifdef DISPLAY_TYPE_SH1106
        display = new Adafruit_SH1106G(DISPLAY_WIDTH, DISPLAY_HEIGHT, &Wire, -1);
        #ifndef DISPLAY_ADDR
            #define DISPLAY_ADDR 0x3C
        #endif
        if (!display->begin(DISPLAY_ADDR, true)) {
            ERROR("Display::init: SH1106 display not found");
            delete display;
            display = nullptr;
            return false;
        }
    #elif defined(DISPLAY_TYPE_SSD1306)
        display = new Adafruit_SSD1306(DISPLAY_WIDTH, DISPLAY_HEIGHT, &Wire, -1);
        #ifndef DISPLAY_ADDR
            #define DISPLAY_ADDR 0x3C
        #endif
        if (!display->begin(SSD1306_SWITCHCAPVCC, DISPLAY_ADDR)) {
            ERROR("Display::init: SSD1306 display not found");
            delete display;
            display = nullptr;
            return false;
        }
    #else
        ERROR("Display::init: No display type defined");
        return false;
    #endif

    // Configure display
    display->setRotation(0);  // Portrait mode
    display->clearDisplay();
    display->setTextSize(1);
    display->setTextColor(1);  // White
    display->cp437(true);      // Enable extended characters
    display->display();

    _ready = true;
    _start_time = (uint32_t)Utilities::OS::ltime();
    _last_update = _start_time;

    INFO("Display::init: Display initialized successfully");
    return true;
#else
    // Native build - no display support
    return false;
#endif
}

void Display::update() {
    if (!_ready || _blanked) return;

#ifdef ARDUINO
    uint32_t now = (uint32_t)Utilities::OS::ltime();

    // Throttle display updates
    if (now - _last_update < UPDATE_INTERVAL) return;
    _last_update = now;
    _frame_count++;

    // Clear and redraw
    display->clearDisplay();

    // Draw header and content
    draw_header();
    draw_content();

    // Send to display
    display->display();
#endif
}

void Display::set_identity(const Identity& identity) {
    if (identity) {
        _identity_hash = identity.hash();
    }
}

void Display::set_interface(Interface* iface) {
    // Backwards compatibility: clear and add single interface
    _interfaces.clear();
    if (iface) {
        _interfaces.push_back(iface);
    }
}

void Display::add_interface(Interface* iface) {
    if (iface) {
        _interfaces.push_back(iface);
    }
}

void Display::clear_interfaces() {
    _interfaces.clear();
}

void Display::set_ble_central_peers(size_t count) {
    _ble_central_peers = count;
}

void Display::set_ble_peripheral_peers(size_t count) {
    _ble_peripheral_peers = count;
}

void Display::set_auto_peers(size_t count) {
    _auto_peers = count;
}

void Display::set_reticulum(Reticulum* rns) {
    _reticulum = rns;
}

void Display::blank(bool blank) {
    _blanked = blank;
#ifdef ARDUINO
    if (_ready && display) {
        if (blank) {
            display->clearDisplay();
            display->display();
        }
    }
#endif
}

bool Display::ready() {
    return _ready;
}

void Display::set_rssi(float rssi) {
    _rssi = rssi;
}

// Private implementation

void Display::draw_header() {
#ifdef ARDUINO
    // Draw "μRNS" text logo
    display->setTextSize(2);  // 2x size for visibility
    display->setCursor(3, 0);
    // Use CP437 code 230 (0xE6) for μ character
    display->write(0xE6);  // μ
    display->print("RNS");
    display->setTextSize(1);  // Reset to normal size

    // Draw heartbeat indicator (toggles every ~0.5s)
    display->setCursor(DISPLAY_WIDTH - 8, 4);
    display->print((_frame_count / 4) % 2 == 0 ? "*" : " ");

    // Draw separator line
    display->drawLine(0, HEADER_HEIGHT, DISPLAY_WIDTH - 1, HEADER_HEIGHT, 1);
#endif
}

void Display::draw_content() {
#ifdef ARDUINO
    int16_t y = CONTENT_Y;

    // Line 1: BLE and Auto peer counts (most important)
    display->setCursor(LEFT_MARGIN, y);
    display->print("BLE:");
    display->print((int)_ble_central_peers);
    display->print("C;");
    display->print((int)_ble_peripheral_peers);
    display->print("P");
    display->setCursor(70, y);
    display->print("Auto:");
    display->print((int)_auto_peers);
    y += LINE_HEIGHT;

    // Line 2: Interface status (X/Y online)
    display->setCursor(LEFT_MARGIN, y);
    size_t online_count = 0;
    for (auto* iface : _interfaces) {
        if (iface && iface->online()) online_count++;
    }
    display->print("Ifaces: ");
    display->print((int)online_count);
    display->print("/");
    display->print((int)_interfaces.size());
    display->print(" online");
    y += LINE_HEIGHT;

    // Line 3: Links and paths
    display->setCursor(LEFT_MARGIN, y);
    display->print("Links: ");
    size_t link_count = _reticulum ? _reticulum->get_link_count() : 0;
    display->print((int)link_count);
    display->setCursor(64, y);
    display->print("Paths: ");
    size_t path_count = _reticulum ? _reticulum->get_path_table().size() : 0;
    display->print((int)path_count);
    y += LINE_HEIGHT;

    // Line 4: Uptime
    display->setCursor(LEFT_MARGIN, y);
    display->print("Up: ");
    uint32_t uptime_sec = ((uint32_t)Utilities::OS::ltime() - _start_time) / 1000;
    display->print(format_time(uptime_sec).c_str());
#endif
}

std::string Display::format_time(uint32_t seconds) {
    char buf[16];
    if (seconds >= 3600) {
        uint32_t hours = seconds / 3600;
        uint32_t mins = (seconds % 3600) / 60;
        snprintf(buf, sizeof(buf), "%luh %lum", (unsigned long)hours, (unsigned long)mins);
    } else if (seconds >= 60) {
        uint32_t mins = seconds / 60;
        uint32_t secs = seconds % 60;
        snprintf(buf, sizeof(buf), "%lum %lus", (unsigned long)mins, (unsigned long)secs);
    } else {
        snprintf(buf, sizeof(buf), "%lus", (unsigned long)seconds);
    }
    return std::string(buf);
}

} // namespace RNS

#endif // HAS_DISPLAY
