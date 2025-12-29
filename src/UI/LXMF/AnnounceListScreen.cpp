// Copyright (c) 2024 microReticulum contributors
// SPDX-License-Identifier: MIT

#include "AnnounceListScreen.h"

#ifdef ARDUINO

#include "../../Log.h"
#include "../../Transport.h"
#include "../../Identity.h"
#include "../../Utilities/OS.h"

using namespace RNS;

namespace UI {
namespace LXMF {

AnnounceListScreen::AnnounceListScreen(lv_obj_t* parent)
    : _screen(nullptr), _header(nullptr), _list(nullptr),
      _btn_back(nullptr), _btn_refresh(nullptr), _empty_label(nullptr) {

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
    lv_obj_set_style_pad_all(_screen, 0, 0);
    lv_obj_set_style_border_width(_screen, 0, 0);
    lv_obj_set_style_radius(_screen, 0, 0);

    // Create UI components
    create_header();
    create_list();

    // Hide by default
    hide();

    TRACE("AnnounceListScreen created");
}

AnnounceListScreen::~AnnounceListScreen() {
    if (_screen) {
        lv_obj_del(_screen);
    }
}

void AnnounceListScreen::create_header() {
    _header = lv_obj_create(_screen);
    lv_obj_set_size(_header, LV_PCT(100), 36);
    lv_obj_align(_header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(_header, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_width(_header, 0, 0);
    lv_obj_set_style_radius(_header, 0, 0);
    lv_obj_set_style_pad_all(_header, 0, 0);

    // Back button
    _btn_back = lv_btn_create(_header);
    lv_obj_set_size(_btn_back, 50, 28);
    lv_obj_align(_btn_back, LV_ALIGN_LEFT_MID, 2, 0);
    lv_obj_set_style_bg_color(_btn_back, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_color(_btn_back, lv_color_hex(0x444444), LV_STATE_PRESSED);
    lv_obj_add_event_cb(_btn_back, on_back_clicked, LV_EVENT_CLICKED, this);

    lv_obj_t* label_back = lv_label_create(_btn_back);
    lv_label_set_text(label_back, LV_SYMBOL_LEFT);
    lv_obj_center(label_back);
    lv_obj_set_style_text_color(label_back, lv_color_hex(0xe0e0e0), 0);

    // Title
    lv_obj_t* title = lv_label_create(_header);
    lv_label_set_text(title, "Announces");
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 60, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);

    // Refresh button
    _btn_refresh = lv_btn_create(_header);
    lv_obj_set_size(_btn_refresh, 65, 28);
    lv_obj_align(_btn_refresh, LV_ALIGN_RIGHT_MID, -2, 0);
    lv_obj_set_style_bg_color(_btn_refresh, lv_color_hex(0x1976D2), 0);
    lv_obj_set_style_bg_color(_btn_refresh, lv_color_hex(0x2196F3), LV_STATE_PRESSED);
    lv_obj_add_event_cb(_btn_refresh, on_refresh_clicked, LV_EVENT_CLICKED, this);

    lv_obj_t* label_refresh = lv_label_create(_btn_refresh);
    lv_label_set_text(label_refresh, LV_SYMBOL_REFRESH);
    lv_obj_center(label_refresh);
    lv_obj_set_style_text_color(label_refresh, lv_color_hex(0xffffff), 0);
}

void AnnounceListScreen::create_list() {
    _list = lv_obj_create(_screen);
    lv_obj_set_size(_list, LV_PCT(100), 204);  // 240 - 36 (header)
    lv_obj_align(_list, LV_ALIGN_TOP_MID, 0, 36);
    lv_obj_set_style_pad_all(_list, 4, 0);
    lv_obj_set_style_pad_gap(_list, 4, 0);
    lv_obj_set_style_bg_color(_list, lv_color_hex(0x121212), 0);
    lv_obj_set_style_border_width(_list, 0, 0);
    lv_obj_set_style_radius(_list, 0, 0);
    lv_obj_set_flex_flow(_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
}

void AnnounceListScreen::refresh() {
    INFO("Refreshing announce list");

    // Clear existing items
    lv_obj_clean(_list);
    _announces.clear();
    _empty_label = nullptr;

    // Get destination table from Transport
    const auto& dest_table = Transport::get_destination_table();

    for (auto it = dest_table.begin(); it != dest_table.end(); ++it) {
        const Bytes& dest_hash = it->first;
        const Transport::DestinationEntry& dest_entry = it->second;

        // Check if this destination has a known identity (was announced properly)
        Identity identity = Identity::recall(dest_hash);

        AnnounceItem item;
        item.destination_hash = dest_hash;
        item.hash_display = truncate_hash(dest_hash);
        item.hops = dest_entry._hops;
        item.timestamp = dest_entry._timestamp;
        item.timestamp_str = format_timestamp(dest_entry._timestamp);
        item.has_path = Transport::has_path(dest_hash);

        _announces.push_back(item);
    }

    // Sort by timestamp (newest first)
    std::sort(_announces.begin(), _announces.end(),
        [](const AnnounceItem& a, const AnnounceItem& b) {
            return a.timestamp > b.timestamp;
        });

    std::string count_msg = "  Found " + std::to_string(_announces.size()) + " announced destinations";
    INFO(count_msg.c_str());

    if (_announces.empty()) {
        show_empty_state();
    } else {
        for (const auto& item : _announces) {
            create_announce_item(item);
        }
    }
}

void AnnounceListScreen::show_empty_state() {
    _empty_label = lv_label_create(_list);
    lv_label_set_text(_empty_label, "No announces yet\n\nWaiting for LXMF\ndestinations to announce...");
    lv_obj_set_style_text_color(_empty_label, lv_color_hex(0x808080), 0);
    lv_obj_set_style_text_align(_empty_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(_empty_label, LV_ALIGN_CENTER, 0, 0);
}

void AnnounceListScreen::create_announce_item(const AnnounceItem& item) {
    // Create container for announce item - compact 2-row layout matching ConversationListScreen
    lv_obj_t* container = lv_obj_create(_list);
    lv_obj_set_size(container, LV_PCT(100), 44);
    lv_obj_set_style_bg_color(container, lv_color_hex(0x2E2E2E), 0);
    lv_obj_set_style_bg_color(container, lv_color_hex(0x3E3E3E), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(container, 1, 0);
    lv_obj_set_style_border_color(container, lv_color_hex(0x404040), 0);
    lv_obj_set_style_radius(container, 6, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_add_flag(container, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);

    // Store destination hash in user data
    Bytes* hash_copy = new Bytes(item.destination_hash);
    lv_obj_set_user_data(container, hash_copy);
    lv_obj_add_event_cb(container, on_announce_clicked, LV_EVENT_CLICKED, this);

    // Row 1: Destination hash (left) + Timestamp (right)
    lv_obj_t* label_hash = lv_label_create(container);
    lv_label_set_text(label_hash, item.hash_display.c_str());
    lv_obj_align(label_hash, LV_ALIGN_TOP_LEFT, 6, 4);
    lv_obj_set_style_text_color(label_hash, lv_color_hex(0x42A5F5), 0);
    lv_obj_set_style_text_font(label_hash, &lv_font_montserrat_14, 0);

    lv_obj_t* label_time = lv_label_create(container);
    lv_label_set_text(label_time, item.timestamp_str.c_str());
    lv_obj_align(label_time, LV_ALIGN_TOP_RIGHT, -6, 6);
    lv_obj_set_style_text_color(label_time, lv_color_hex(0x808080), 0);

    // Row 2: Hops info (left) + Status dot (right)
    lv_obj_t* label_hops = lv_label_create(container);
    lv_label_set_text(label_hops, format_hops(item.hops).c_str());
    lv_obj_align(label_hops, LV_ALIGN_BOTTOM_LEFT, 6, -4);
    lv_obj_set_style_text_color(label_hops, lv_color_hex(0xB0B0B0), 0);

    // Status indicator (green dot if has path)
    if (item.has_path) {
        lv_obj_t* status_dot = lv_obj_create(container);
        lv_obj_set_size(status_dot, 8, 8);
        lv_obj_align(status_dot, LV_ALIGN_BOTTOM_RIGHT, -6, -8);
        lv_obj_set_style_bg_color(status_dot, lv_color_hex(0x4CAF50), 0);
        lv_obj_set_style_radius(status_dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(status_dot, 0, 0);
        lv_obj_set_style_pad_all(status_dot, 0, 0);
    }
}

void AnnounceListScreen::set_announce_selected_callback(AnnounceSelectedCallback callback) {
    _announce_selected_callback = callback;
}

void AnnounceListScreen::set_back_callback(BackCallback callback) {
    _back_callback = callback;
}

void AnnounceListScreen::show() {
    lv_obj_clear_flag(_screen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(_screen);
}

void AnnounceListScreen::hide() {
    lv_obj_add_flag(_screen, LV_OBJ_FLAG_HIDDEN);
}

lv_obj_t* AnnounceListScreen::get_object() {
    return _screen;
}

void AnnounceListScreen::on_announce_clicked(lv_event_t* event) {
    AnnounceListScreen* screen = (AnnounceListScreen*)lv_event_get_user_data(event);
    lv_obj_t* target = lv_event_get_target(event);

    Bytes* dest_hash = (Bytes*)lv_obj_get_user_data(target);

    if (dest_hash && screen->_announce_selected_callback) {
        screen->_announce_selected_callback(*dest_hash);
    }
}

void AnnounceListScreen::on_back_clicked(lv_event_t* event) {
    AnnounceListScreen* screen = (AnnounceListScreen*)lv_event_get_user_data(event);

    if (screen->_back_callback) {
        screen->_back_callback();
    }
}

void AnnounceListScreen::on_refresh_clicked(lv_event_t* event) {
    AnnounceListScreen* screen = (AnnounceListScreen*)lv_event_get_user_data(event);
    screen->refresh();
}

String AnnounceListScreen::format_timestamp(double timestamp) {
    double now = Utilities::OS::time();
    double diff = now - timestamp;

    if (diff < 60) {
        return "Just now";
    } else if (diff < 3600) {
        int mins = (int)(diff / 60);
        return String(mins) + "m ago";
    } else if (diff < 86400) {
        int hours = (int)(diff / 3600);
        return String(hours) + "h ago";
    } else {
        int days = (int)(diff / 86400);
        return String(days) + "d ago";
    }
}

String AnnounceListScreen::format_hops(uint8_t hops) {
    if (hops == 0) {
        return "Direct";
    } else if (hops == 1) {
        return "1 hop";
    } else {
        return String(hops) + " hops";
    }
}

String AnnounceListScreen::truncate_hash(const Bytes& hash) {
    if (hash.size() < 8) {
        return String(hash.toHex().c_str());
    }

    // Show first 12 hex chars (6 bytes)
    String hex = String(hash.toHex().c_str());
    return hex.substring(0, 12) + "...";
}

} // namespace LXMF
} // namespace UI

#endif // ARDUINO
