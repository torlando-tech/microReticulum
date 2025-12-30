// Copyright (c) 2024 microReticulum contributors
// SPDX-License-Identifier: MIT

#ifndef UI_LXMF_STATUSSCREEN_H
#define UI_LXMF_STATUSSCREEN_H

#ifdef ARDUINO
#include <Arduino.h>
#include <lvgl.h>
#include <functional>
#include "../../Bytes.h"
#include "../../Identity.h"

namespace UI {
namespace LXMF {

/**
 * Status Screen
 *
 * Shows network and identity information:
 * - Identity hash
 * - LXMF delivery destination hash
 * - WiFi status and IP
 * - RNS connection status
 *
 * Layout:
 * ┌─────────────────────────────────────┐
 * │ ← Status                            │ 32px header
 * ├─────────────────────────────────────┤
 * │ Identity:                           │
 * │   a1b2c3d4e5f6...                   │
 * │                                     │
 * │ LXMF Address:                       │
 * │   f7e8d9c0b1a2...                   │
 * │                                     │
 * │ WiFi: Connected                     │
 * │   IP: 192.168.1.100                 │
 * │   RSSI: -65 dBm                     │
 * │                                     │
 * │ RNS: Connected                      │
 * └─────────────────────────────────────┘
 */
class StatusScreen {
public:
    using BackCallback = std::function<void()>;
    using ShareCallback = std::function<void()>;

    /**
     * Create status screen
     * @param parent Parent LVGL object
     */
    StatusScreen(lv_obj_t* parent = nullptr);

    /**
     * Destructor
     */
    ~StatusScreen();

    /**
     * Set identity hash to display
     * @param hash The identity hash
     */
    void set_identity_hash(const RNS::Bytes& hash);

    /**
     * Set LXMF delivery destination hash
     * @param hash The delivery destination hash
     */
    void set_lxmf_address(const RNS::Bytes& hash);

    /**
     * Set RNS connection status
     * @param connected Whether connected to RNS server
     * @param server_name Server hostname
     */
    void set_rns_status(bool connected, const String& server_name = "");

    /**
     * Refresh WiFi and connection status
     */
    void refresh();

    /**
     * Set callback for back button
     * @param callback Function to call when back is pressed
     */
    void set_back_callback(BackCallback callback);

    /**
     * Set callback for share button
     * @param callback Function to call when share is pressed
     */
    void set_share_callback(ShareCallback callback);

    /**
     * Show the screen
     */
    void show();

    /**
     * Hide the screen
     */
    void hide();

    /**
     * Get the root LVGL object
     */
    lv_obj_t* get_object();

private:
    lv_obj_t* _screen;
    lv_obj_t* _header;
    lv_obj_t* _content;
    lv_obj_t* _btn_back;
    lv_obj_t* _btn_share;

    // Labels for dynamic content
    lv_obj_t* _label_identity_value;
    lv_obj_t* _label_lxmf_value;
    lv_obj_t* _label_wifi_status;
    lv_obj_t* _label_wifi_ip;
    lv_obj_t* _label_wifi_rssi;
    lv_obj_t* _label_rns_status;

    RNS::Bytes _identity_hash;
    RNS::Bytes _lxmf_address;
    bool _rns_connected;
    String _rns_server;

    BackCallback _back_callback;
    ShareCallback _share_callback;

    void create_header();
    void create_content();
    void update_labels();

    static void on_back_clicked(lv_event_t* event);
    static void on_share_clicked(lv_event_t* event);
};

} // namespace LXMF
} // namespace UI

#endif // ARDUINO
#endif // UI_LXMF_STATUSSCREEN_H
