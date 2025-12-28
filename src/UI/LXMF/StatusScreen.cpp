// Copyright (c) 2024 microReticulum contributors
// SPDX-License-Identifier: MIT

#include "StatusScreen.h"

#ifdef ARDUINO

#include <WiFi.h>
#include "../../Log.h"

using namespace RNS;

namespace UI {
namespace LXMF {

StatusScreen::StatusScreen(lv_obj_t* parent)
    : _screen(nullptr), _header(nullptr), _content(nullptr), _btn_back(nullptr),
      _label_identity_value(nullptr), _label_lxmf_value(nullptr),
      _label_wifi_status(nullptr), _label_wifi_ip(nullptr), _label_wifi_rssi(nullptr),
      _label_rns_status(nullptr), _rns_connected(false) {

    // Create screen object
    if (parent) {
        _screen = lv_obj_create(parent);
    } else {
        _screen = lv_obj_create(lv_scr_act());
    }

    lv_obj_set_size(_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(_screen, lv_color_hex(0x121212), 0);
    lv_obj_set_style_bg_opa(_screen, LV_OPA_COVER, 0);

    // Create UI components
    create_header();
    create_content();

    // Hide by default
    hide();

    TRACE("StatusScreen created");
}

StatusScreen::~StatusScreen() {
    if (_screen) {
        lv_obj_del(_screen);
    }
}

void StatusScreen::create_header() {
    _header = lv_obj_create(_screen);
    lv_obj_set_size(_header, LV_PCT(100), 36);
    lv_obj_align(_header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(_header, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_width(_header, 0, 0);
    lv_obj_set_style_radius(_header, 0, 0);

    // Back button
    _btn_back = lv_btn_create(_header);
    lv_obj_set_size(_btn_back, 50, 28);
    lv_obj_align(_btn_back, LV_ALIGN_LEFT_MID, 5, 0);
    lv_obj_set_style_bg_color(_btn_back, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_color(_btn_back, lv_color_hex(0x444444), LV_STATE_PRESSED);
    lv_obj_add_event_cb(_btn_back, on_back_clicked, LV_EVENT_CLICKED, this);

    lv_obj_t* label_back = lv_label_create(_btn_back);
    lv_label_set_text(label_back, LV_SYMBOL_LEFT);
    lv_obj_center(label_back);
    lv_obj_set_style_text_color(label_back, lv_color_hex(0xe0e0e0), 0);

    // Title
    lv_obj_t* title = lv_label_create(_header);
    lv_label_set_text(title, "Status");
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 60, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
}

void StatusScreen::create_content() {
    _content = lv_obj_create(_screen);
    lv_obj_set_size(_content, LV_PCT(100), 204);  // 240 - 36 header
    lv_obj_align(_content, LV_ALIGN_TOP_MID, 0, 36);
    lv_obj_set_style_pad_all(_content, 12, 0);
    lv_obj_set_style_bg_color(_content, lv_color_hex(0x121212), 0);
    lv_obj_set_style_border_width(_content, 0, 0);

    // Enable vertical scrolling
    lv_obj_set_scroll_dir(_content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(_content, LV_SCROLLBAR_MODE_AUTO);

    int y_pos = 0;
    const int line_height = 18;
    const int section_gap = 12;

    // Identity section
    lv_obj_t* label_identity = lv_label_create(_content);
    lv_label_set_text(label_identity, "Identity:");
    lv_obj_align(label_identity, LV_ALIGN_TOP_LEFT, 0, y_pos);
    lv_obj_set_style_text_color(label_identity, lv_color_hex(0x808080), 0);
    y_pos += line_height;

    _label_identity_value = lv_label_create(_content);
    lv_label_set_text(_label_identity_value, "Loading...");
    lv_obj_align(_label_identity_value, LV_ALIGN_TOP_LEFT, 8, y_pos);
    lv_obj_set_style_text_color(_label_identity_value, lv_color_hex(0x42A5F5), 0);
    lv_obj_set_style_text_font(_label_identity_value, &lv_font_montserrat_12, 0);
    y_pos += line_height + section_gap;

    // LXMF Address section
    lv_obj_t* label_lxmf = lv_label_create(_content);
    lv_label_set_text(label_lxmf, "LXMF Address:");
    lv_obj_align(label_lxmf, LV_ALIGN_TOP_LEFT, 0, y_pos);
    lv_obj_set_style_text_color(label_lxmf, lv_color_hex(0x808080), 0);
    y_pos += line_height;

    _label_lxmf_value = lv_label_create(_content);
    lv_label_set_text(_label_lxmf_value, "Loading...");
    lv_obj_align(_label_lxmf_value, LV_ALIGN_TOP_LEFT, 8, y_pos);
    lv_obj_set_style_text_color(_label_lxmf_value, lv_color_hex(0x4CAF50), 0);
    lv_obj_set_style_text_font(_label_lxmf_value, &lv_font_montserrat_12, 0);
    y_pos += line_height + section_gap;

    // WiFi section
    _label_wifi_status = lv_label_create(_content);
    lv_label_set_text(_label_wifi_status, "WiFi: Checking...");
    lv_obj_align(_label_wifi_status, LV_ALIGN_TOP_LEFT, 0, y_pos);
    lv_obj_set_style_text_color(_label_wifi_status, lv_color_hex(0xffffff), 0);
    y_pos += line_height;

    _label_wifi_ip = lv_label_create(_content);
    lv_label_set_text(_label_wifi_ip, "");
    lv_obj_align(_label_wifi_ip, LV_ALIGN_TOP_LEFT, 8, y_pos);
    lv_obj_set_style_text_color(_label_wifi_ip, lv_color_hex(0xb0b0b0), 0);
    y_pos += line_height;

    _label_wifi_rssi = lv_label_create(_content);
    lv_label_set_text(_label_wifi_rssi, "");
    lv_obj_align(_label_wifi_rssi, LV_ALIGN_TOP_LEFT, 8, y_pos);
    lv_obj_set_style_text_color(_label_wifi_rssi, lv_color_hex(0xb0b0b0), 0);
    y_pos += line_height + section_gap;

    // RNS section
    _label_rns_status = lv_label_create(_content);
    lv_label_set_text(_label_rns_status, "RNS: Checking...");
    lv_obj_align(_label_rns_status, LV_ALIGN_TOP_LEFT, 0, y_pos);
    lv_obj_set_style_text_color(_label_rns_status, lv_color_hex(0xffffff), 0);
    lv_obj_set_width(_label_rns_status, lv_pct(95));  // Set width for text wrapping
    lv_label_set_long_mode(_label_rns_status, LV_LABEL_LONG_WRAP);
}

void StatusScreen::set_identity(const Identity& identity) {
    _identity_hash = identity.hash();
    update_labels();
}

void StatusScreen::set_lxmf_address(const Bytes& hash) {
    _lxmf_address = hash;
    update_labels();
}

void StatusScreen::set_rns_status(bool connected, const String& server_name) {
    _rns_connected = connected;
    _rns_server = server_name;
    update_labels();
}

void StatusScreen::refresh() {
    update_labels();
}

void StatusScreen::update_labels() {
    // Update identity
    if (_identity_hash.size() > 0) {
        String hash_str = String(_identity_hash.toHex().c_str());
        lv_label_set_text(_label_identity_value, hash_str.c_str());
    }

    // Update LXMF address
    if (_lxmf_address.size() > 0) {
        String hash_str = String(_lxmf_address.toHex().c_str());
        lv_label_set_text(_label_lxmf_value, hash_str.c_str());
    }

    // Update WiFi status
    if (WiFi.status() == WL_CONNECTED) {
        lv_label_set_text(_label_wifi_status, "WiFi: Connected");
        lv_obj_set_style_text_color(_label_wifi_status, lv_color_hex(0x4CAF50), 0);

        String ip_text = "IP: " + WiFi.localIP().toString();
        lv_label_set_text(_label_wifi_ip, ip_text.c_str());

        String rssi_text = "RSSI: " + String(WiFi.RSSI()) + " dBm";
        lv_label_set_text(_label_wifi_rssi, rssi_text.c_str());
    } else {
        lv_label_set_text(_label_wifi_status, "WiFi: Disconnected");
        lv_obj_set_style_text_color(_label_wifi_status, lv_color_hex(0xF44336), 0);
        lv_label_set_text(_label_wifi_ip, "");
        lv_label_set_text(_label_wifi_rssi, "");
    }

    // Update RNS status
    if (_rns_connected) {
        String rns_text = "RNS: Connected";
        if (_rns_server.length() > 0) {
            rns_text += " (" + _rns_server + ")";
        }
        lv_label_set_text(_label_rns_status, rns_text.c_str());
        lv_obj_set_style_text_color(_label_rns_status, lv_color_hex(0x4CAF50), 0);
    } else {
        lv_label_set_text(_label_rns_status, "RNS: Disconnected");
        lv_obj_set_style_text_color(_label_rns_status, lv_color_hex(0xF44336), 0);
    }
}

void StatusScreen::set_back_callback(BackCallback callback) {
    _back_callback = callback;
}

void StatusScreen::show() {
    refresh();  // Update status when shown
    lv_obj_clear_flag(_screen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(_screen);
}

void StatusScreen::hide() {
    lv_obj_add_flag(_screen, LV_OBJ_FLAG_HIDDEN);
}

lv_obj_t* StatusScreen::get_object() {
    return _screen;
}

void StatusScreen::on_back_clicked(lv_event_t* event) {
    StatusScreen* screen = (StatusScreen*)lv_event_get_user_data(event);

    if (screen->_back_callback) {
        screen->_back_callback();
    }
}

} // namespace LXMF
} // namespace UI

#endif // ARDUINO
