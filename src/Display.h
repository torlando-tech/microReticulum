/*
 * Display.h - OLED display support for microReticulum
 *
 * Provides single-screen status display on supported hardware (T-Beam Supreme, etc.)
 * showing connection status, peer counts, and network statistics.
 */

#pragma once

#include "Type.h"
#include "Bytes.h"

#include <stdint.h>
#include <string>
#include <vector>

// Only compile display code if enabled
#ifdef HAS_DISPLAY

namespace RNS {

// Forward declarations
class Identity;
class Interface;
class Reticulum;

class Display {
public:
    // Display update interval (~7 FPS)
    static const uint32_t UPDATE_INTERVAL = 143;

public:
    /**
     * Initialize the display hardware.
     * Must be called once during setup.
     * @return true if display initialized successfully
     */
    static bool init();

    /**
     * Update the display. Call this frequently in the main loop.
     */
    static void update();

    /**
     * Set the identity to display.
     * @param identity The identity whose hash will be shown
     */
    static void set_identity(const Identity& identity);

    /**
     * Set the primary interface to display status for.
     * @param iface Pointer to the interface (can be nullptr)
     * @deprecated Use add_interface() for multiple interfaces
     */
    static void set_interface(Interface* iface);

    /**
     * Add an interface to display status for.
     * @param iface Pointer to the interface (can be nullptr)
     */
    static void add_interface(Interface* iface);

    /**
     * Clear all registered interfaces.
     */
    static void clear_interfaces();

    /**
     * Set the Reticulum instance for network statistics.
     * @param rns Pointer to the Reticulum instance
     */
    static void set_reticulum(Reticulum* rns);

    /**
     * Enable or disable display blanking (power save).
     * @param blank true to blank the display
     */
    static void blank(bool blank);

    /**
     * Check if display is ready.
     * @return true if display was initialized successfully
     */
    static bool ready();

    /**
     * Set RSSI value for signal bars display.
     * @param rssi Signal strength in dBm
     */
    static void set_rssi(float rssi);

    /**
     * Set BLE central peer count for display.
     * @param count Number of peripherals we're connected to as central
     */
    static void set_ble_central_peers(size_t count);

    /**
     * Set BLE peripheral peer count for display.
     * @param count Number of centrals connected to us as peripheral
     */
    static void set_ble_peripheral_peers(size_t count);

    /**
     * Set Auto interface peer count for display.
     * @param count Number of discovered Auto peers
     */
    static void set_auto_peers(size_t count);

private:
    // Drawing functions
    static void draw_header();
    static void draw_content();

    // Helper functions
    static std::string format_time(uint32_t seconds);

private:
    // State
    static bool _ready;
    static bool _blanked;
    static uint32_t _last_update;
    static uint32_t _start_time;
    static uint32_t _frame_count;

    // Data sources
    static Bytes _identity_hash;
    static std::vector<Interface*> _interfaces;
    static Reticulum* _reticulum;

    // Signal strength
    static float _rssi;

    // Peer counts (set from main loop since Display can't access interface-specific classes)
    static size_t _ble_central_peers;
    static size_t _ble_peripheral_peers;
    static size_t _auto_peers;
};

} // namespace RNS

#endif // HAS_DISPLAY
