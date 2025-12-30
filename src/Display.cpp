/*
 * Display.cpp - OLED display implementation for microReticulum
 */

#include "Display.h"

#ifdef HAS_DISPLAY

#include "DisplayGraphics.h"
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
static const int16_t CONTENT_Y = 21;
static const int16_t LINE_HEIGHT = 10;
static const int16_t LEFT_MARGIN = 2;

// Static member initialization
bool Display::_ready = false;
bool Display::_blanked = false;
uint8_t Display::_current_page = 0;
uint32_t Display::_last_page_flip = 0;
uint32_t Display::_last_update = 0;
uint32_t Display::_start_time = 0;
Bytes Display::_identity_hash;
std::vector<Interface*> Display::_interfaces;
Reticulum* Display::_reticulum = nullptr;
float Display::_rssi = -120.0f;
size_t Display::_ble_peers = 0;
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
    _last_page_flip = _start_time;
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

    // Check for page rotation
    if (now - _last_page_flip >= PAGE_INTERVAL) {
        _current_page = (_current_page + 1) % NUM_PAGES;
        _last_page_flip = now;
    }

    // Throttle display updates
    if (now - _last_update < UPDATE_INTERVAL) return;
    _last_update = now;

    // Clear and redraw
    display->clearDisplay();

    // Draw header (common to all pages)
    draw_header();

    // Draw page-specific content
    switch (_current_page) {
        case 0:
            draw_page_main();
            break;
        case 1:
            draw_page_interface();
            break;
        case 2:
            draw_page_network();
            break;
    }

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

void Display::set_ble_peers(size_t count) {
    _ble_peers = count;
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

void Display::set_page(uint8_t page) {
    if (page < NUM_PAGES) {
        _current_page = page;
        _last_page_flip = (uint32_t)Utilities::OS::ltime();
    }
}

void Display::next_page() {
    _current_page = (_current_page + 1) % NUM_PAGES;
    _last_page_flip = (uint32_t)Utilities::OS::ltime();
}

uint8_t Display::current_page() {
    return _current_page;
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

    // Draw signal bars on the right
    draw_signal_bars(DISPLAY_WIDTH - Graphics::SIGNAL_WIDTH - 2, 4);

    // Draw separator line
    display->drawLine(0, HEADER_HEIGHT, DISPLAY_WIDTH - 1, HEADER_HEIGHT, 1);
#endif
}

void Display::draw_signal_bars(int16_t x, int16_t y) {
#ifdef ARDUINO
    // Temporarily disabled to debug stray pixel
    // uint8_t level = 0;
    // if (_interface && _interface->online()) {
    //     level = Graphics::rssi_to_level(_rssi);
    // }
    // const uint8_t* bitmap = Graphics::get_signal_bitmap(level);
    // display->drawBitmap(x, y, bitmap, Graphics::SIGNAL_WIDTH, Graphics::SIGNAL_HEIGHT, 1);
#endif
}

void Display::draw_page_main() {
#ifdef ARDUINO
    int16_t y = CONTENT_Y;

    // Identity hash
    display->setCursor(LEFT_MARGIN, y);
    display->print("ID: ");
    if (_identity_hash.size() > 0) {
        // Show first 12 hex chars
        std::string hex = _identity_hash.toHex();
        if (hex.length() > 12) hex = hex.substr(0, 12);
        display->print(hex.c_str());
    } else {
        display->print("(none)");
    }
    y += LINE_HEIGHT + 4;

    // Interface summary (show count of online interfaces)
    display->setCursor(LEFT_MARGIN, y);
    size_t online_count = 0;
    for (auto* iface : _interfaces) {
        if (iface && iface->online()) online_count++;
    }
    display->print("Ifaces: ");
    display->print((int)_interfaces.size());
    display->print(" (");
    display->print((int)online_count);
    display->print(" online)");
    y += LINE_HEIGHT;

    // Link count
    display->setCursor(LEFT_MARGIN, y);
    display->print("Links: ");
    if (_reticulum) {
        display->print((int)_reticulum->get_link_count());
    } else {
        display->print("0");
    }
#endif
}

void Display::draw_page_interface() {
#ifdef ARDUINO
    int16_t y = CONTENT_Y;

    if (_interfaces.empty()) {
        display->setCursor(LEFT_MARGIN, y);
        display->print("No interfaces");
        return;
    }

    // Show each interface with status
    for (auto* iface : _interfaces) {
        if (!iface) continue;

        display->setCursor(LEFT_MARGIN, y);

        // Interface name (truncate if needed)
        std::string name = iface->name();
        if (name.length() > 6) name = name.substr(0, 6);
        display->print(name.c_str());
        display->print(": ");

        // Online/offline status
        display->print(iface->online() ? "ON " : "OFF");

        // For compact display, show bitrate in short form
        uint32_t bps = iface->bitrate();
        if (bps >= 1000000) {
            char buf[8];
            snprintf(buf, sizeof(buf), " %.0fM", bps / 1000000.0);
            display->print(buf);
        } else if (bps >= 1000) {
            char buf[8];
            snprintf(buf, sizeof(buf), " %.0fk", bps / 1000.0);
            display->print(buf);
        }

        y += LINE_HEIGHT;

        // Stop if we'd overflow the display
        if (y > DISPLAY_HEIGHT - LINE_HEIGHT) break;
    }
#endif
}

void Display::draw_page_network() {
#ifdef ARDUINO
    int16_t y = CONTENT_Y;

    // Links and paths
    display->setCursor(LEFT_MARGIN, y);
    display->print("Links: ");
    size_t link_count = _reticulum ? _reticulum->get_link_count() : 0;
    display->print((int)link_count);

    display->print("  Paths: ");
    size_t path_count = _reticulum ? _reticulum->get_path_table().size() : 0;
    display->print((int)path_count);
    y += LINE_HEIGHT;

    // BLE peer count
    display->setCursor(LEFT_MARGIN, y);
    display->print("BLE Peers: ");
    display->print((int)_ble_peers);
    y += LINE_HEIGHT;

    // Auto interface peer count
    display->setCursor(LEFT_MARGIN, y);
    display->print("Auto Peers: ");
    display->print((int)_auto_peers);
    y += LINE_HEIGHT;

    // Uptime
    display->setCursor(LEFT_MARGIN, y);
    display->print("Uptime: ");
    uint32_t uptime_sec = ((uint32_t)Utilities::OS::ltime() - _start_time) / 1000;
    display->print(format_time(uptime_sec).c_str());
#endif
}

std::string Display::format_bytes(size_t bytes) {
    char buf[16];
    if (bytes >= 1024 * 1024) {
        snprintf(buf, sizeof(buf), "%.1fM", bytes / (1024.0 * 1024.0));
    } else if (bytes >= 1024) {
        snprintf(buf, sizeof(buf), "%.1fK", bytes / 1024.0);
    } else {
        snprintf(buf, sizeof(buf), "%zuB", bytes);
    }
    return std::string(buf);
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

std::string Display::format_bitrate(uint32_t bps) {
    char buf[16];
    if (bps >= 1000000) {
        snprintf(buf, sizeof(buf), "%.1f Mbps", bps / 1000000.0);
    } else if (bps >= 1000) {
        snprintf(buf, sizeof(buf), "%.1f kbps", bps / 1000.0);
    } else {
        snprintf(buf, sizeof(buf), "%lu bps", (unsigned long)bps);
    }
    return std::string(buf);
}

} // namespace RNS

#endif // HAS_DISPLAY
