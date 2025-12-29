// Copyright (c) 2024 microReticulum contributors
// SPDX-License-Identifier: MIT

#include "ConversationListScreen.h"

#ifdef ARDUINO

#include "../../Log.h"
#include "../../Identity.h"
#include "../../Hardware/TDeck/Config.h"
#include <WiFi.h>
#include <MsgPack.h>

using namespace RNS;
using namespace Hardware::TDeck;

namespace UI {
namespace LXMF {

ConversationListScreen::ConversationListScreen(lv_obj_t* parent)
    : _screen(nullptr), _header(nullptr), _list(nullptr), _bottom_nav(nullptr),
      _btn_new(nullptr), _btn_settings(nullptr), _label_wifi(nullptr), _label_battery(nullptr),
      _message_store(nullptr) {

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
    create_bottom_nav();

    TRACE("ConversationListScreen created");
}

ConversationListScreen::~ConversationListScreen() {
    if (_screen) {
        lv_obj_del(_screen);
    }
}

void ConversationListScreen::create_header() {
    _header = lv_obj_create(_screen);
    lv_obj_set_size(_header, LV_PCT(100), 36);
    lv_obj_align(_header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(_header, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_width(_header, 0, 0);
    lv_obj_set_style_radius(_header, 0, 0);
    lv_obj_set_style_pad_all(_header, 0, 0);

    // Title
    lv_obj_t* title = lv_label_create(_header);
    lv_label_set_text(title, "Messages");
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 8, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);

    // Status indicators (WiFi RSSI and Battery) - positioned in center-right area
    _label_wifi = lv_label_create(_header);
    lv_label_set_text(_label_wifi, LV_SYMBOL_WIFI " --");
    lv_obj_align(_label_wifi, LV_ALIGN_LEFT_MID, 95, 0);
    lv_obj_set_style_text_color(_label_wifi, lv_color_hex(0x808080), 0);

    _label_battery = lv_label_create(_header);
    lv_label_set_text(_label_battery, LV_SYMBOL_BATTERY_FULL " --%");
    lv_obj_align(_label_battery, LV_ALIGN_LEFT_MID, 145, 0);
    lv_obj_set_style_text_color(_label_battery, lv_color_hex(0x808080), 0);

    // New message button
    _btn_new = lv_btn_create(_header);
    lv_obj_set_size(_btn_new, 55, 28);
    lv_obj_align(_btn_new, LV_ALIGN_RIGHT_MID, -48, 0);
    lv_obj_set_style_bg_color(_btn_new, lv_color_hex(0x2e7d32), 0);
    lv_obj_set_style_bg_color(_btn_new, lv_color_hex(0x388e3c), LV_STATE_PRESSED);
    lv_obj_add_event_cb(_btn_new, on_new_message_clicked, LV_EVENT_CLICKED, this);

    lv_obj_t* label_new = lv_label_create(_btn_new);
    lv_label_set_text(label_new, "New");
    lv_obj_center(label_new);
    lv_obj_set_style_text_color(label_new, lv_color_hex(0xffffff), 0);

    // Settings button
    _btn_settings = lv_btn_create(_header);
    lv_obj_set_size(_btn_settings, 40, 28);
    lv_obj_align(_btn_settings, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_set_style_bg_color(_btn_settings, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_color(_btn_settings, lv_color_hex(0x444444), LV_STATE_PRESSED);
    lv_obj_add_event_cb(_btn_settings, on_settings_clicked, LV_EVENT_CLICKED, this);

    lv_obj_t* label_settings = lv_label_create(_btn_settings);
    lv_label_set_text(label_settings, LV_SYMBOL_SETTINGS);
    lv_obj_center(label_settings);
    lv_obj_set_style_text_color(label_settings, lv_color_hex(0xe0e0e0), 0);
}

void ConversationListScreen::create_list() {
    _list = lv_obj_create(_screen);
    lv_obj_set_size(_list, LV_PCT(100), 168);  // 240 - 36 (header) - 36 (bottom nav)
    lv_obj_align(_list, LV_ALIGN_TOP_MID, 0, 36);
    lv_obj_set_style_pad_all(_list, 2, 0);
    lv_obj_set_style_pad_gap(_list, 2, 0);
    lv_obj_set_style_bg_color(_list, lv_color_hex(0x121212), 0);
    lv_obj_set_style_border_width(_list, 0, 0);
    lv_obj_set_style_radius(_list, 0, 0);
    lv_obj_set_flex_flow(_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
}

void ConversationListScreen::create_bottom_nav() {
    _bottom_nav = lv_obj_create(_screen);
    lv_obj_set_size(_bottom_nav, LV_PCT(100), 36);
    lv_obj_align(_bottom_nav, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(_bottom_nav, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_width(_bottom_nav, 0, 0);
    lv_obj_set_style_radius(_bottom_nav, 0, 0);
    lv_obj_set_style_pad_all(_bottom_nav, 0, 0);
    lv_obj_set_flex_flow(_bottom_nav, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(_bottom_nav, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Bottom navigation buttons: Messages, Announces, Status, Settings
    const char* icons[] = {LV_SYMBOL_ENVELOPE, LV_SYMBOL_LIST, LV_SYMBOL_WIFI, LV_SYMBOL_SETTINGS};

    for (int i = 0; i < 4; i++) {
        lv_obj_t* btn = lv_btn_create(_bottom_nav);
        lv_obj_set_size(btn, 65, 28);
        lv_obj_set_user_data(btn, (void*)(intptr_t)i);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x2a2a2a), 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x3a3a3a), LV_STATE_PRESSED);
        lv_obj_add_event_cb(btn, on_bottom_nav_clicked, LV_EVENT_CLICKED, this);

        lv_obj_t* label = lv_label_create(btn);
        lv_label_set_text(label, icons[i]);
        lv_obj_center(label);
        lv_obj_set_style_text_color(label, lv_color_hex(0xb0b0b0), 0);
    }
}

void ConversationListScreen::load_conversations(::LXMF::MessageStore& store) {
    _message_store = &store;
    refresh();
}

void ConversationListScreen::refresh() {
    if (!_message_store) {
        return;
    }

    INFO("Refreshing conversation list");

    // Clear existing items
    lv_obj_clean(_list);
    _conversations.clear();

    // Load conversations from store
    std::vector<Bytes> peer_hashes = _message_store->get_conversations();

    std::string count_msg = "  Found " + std::to_string(peer_hashes.size()) + " conversations";
    INFO(count_msg.c_str());

    for (const auto& peer_hash : peer_hashes) {
        std::vector<Bytes> messages = _message_store->get_messages_for_conversation(peer_hash);

        if (messages.empty()) {
            continue;
        }

        // Load last message for preview
        Bytes last_msg_hash = messages.back();
        ::LXMF::LXMessage last_msg = _message_store->load_message(last_msg_hash);

        // Create conversation item
        ConversationItem item;
        item.peer_hash = peer_hash;

        // Try to get display name from app_data, fall back to hash
        Bytes app_data = Identity::recall_app_data(peer_hash);
        if (app_data && app_data.size() > 0) {
            String display_name = parse_display_name(app_data);
            if (display_name.length() > 0) {
                item.peer_name = display_name;
            } else {
                item.peer_name = truncate_hash(peer_hash);
            }
        } else {
            item.peer_name = truncate_hash(peer_hash);
        }

        // Get message content for preview
        String content((const char*)last_msg.content().data(), last_msg.content().size());
        item.last_message = content.substring(0, 30);  // Truncate to 30 chars
        if (content.length() > 30) {
            item.last_message += "...";
        }

        item.timestamp = (uint32_t)last_msg.timestamp();
        item.timestamp_str = format_timestamp(item.timestamp);
        item.unread_count = 0;  // TODO: Track unread count

        _conversations.push_back(item);
        create_conversation_item(item);
    }
}

void ConversationListScreen::create_conversation_item(const ConversationItem& item) {
    // Create container for conversation item - compact 2-row layout
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

    // Store peer hash in user data
    Bytes* peer_hash_copy = new Bytes(item.peer_hash);
    lv_obj_set_user_data(container, peer_hash_copy);
    lv_obj_add_event_cb(container, on_conversation_clicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(container, on_conversation_long_pressed, LV_EVENT_LONG_PRESSED, this);

    // Row 1: Peer hash
    lv_obj_t* label_peer = lv_label_create(container);
    lv_label_set_text(label_peer, item.peer_name.c_str());
    lv_obj_align(label_peer, LV_ALIGN_TOP_LEFT, 6, 4);
    lv_obj_set_style_text_color(label_peer, lv_color_hex(0x42A5F5), 0);
    lv_obj_set_style_text_font(label_peer, &lv_font_montserrat_14, 0);

    // Row 2: Message preview (left) + Timestamp (right)
    lv_obj_t* label_preview = lv_label_create(container);
    lv_label_set_text(label_preview, item.last_message.c_str());
    lv_obj_align(label_preview, LV_ALIGN_BOTTOM_LEFT, 6, -4);
    lv_obj_set_style_text_color(label_preview, lv_color_hex(0xB0B0B0), 0);
    lv_obj_set_width(label_preview, 200);  // Limit width to leave room for timestamp
    lv_label_set_long_mode(label_preview, LV_LABEL_LONG_DOT);

    lv_obj_t* label_time = lv_label_create(container);
    lv_label_set_text(label_time, item.timestamp_str.c_str());
    lv_obj_align(label_time, LV_ALIGN_BOTTOM_RIGHT, -6, -4);
    lv_obj_set_style_text_color(label_time, lv_color_hex(0x808080), 0);

    // Unread count badge
    if (item.unread_count > 0) {
        lv_obj_t* badge = lv_obj_create(container);
        lv_obj_set_size(badge, 20, 20);
        lv_obj_align(badge, LV_ALIGN_BOTTOM_RIGHT, -6, -4);
        lv_obj_set_style_bg_color(badge, lv_color_hex(0xF44336), 0);
        lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(badge, 0, 0);
        lv_obj_set_style_pad_all(badge, 0, 0);

        lv_obj_t* label_count = lv_label_create(badge);
        lv_label_set_text_fmt(label_count, "%d", item.unread_count);
        lv_obj_center(label_count);
        lv_obj_set_style_text_color(label_count, lv_color_white(), 0);
    }
}

void ConversationListScreen::update_unread_count(const Bytes& peer_hash, uint16_t unread_count) {
    // Find conversation and update
    for (auto& conv : _conversations) {
        if (conv.peer_hash == peer_hash) {
            conv.unread_count = unread_count;
            refresh();  // Redraw list
            break;
        }
    }
}

void ConversationListScreen::set_conversation_selected_callback(ConversationSelectedCallback callback) {
    _conversation_selected_callback = callback;
}

void ConversationListScreen::set_new_message_callback(NewMessageCallback callback) {
    _new_message_callback = callback;
}

void ConversationListScreen::set_settings_callback(SettingsCallback callback) {
    _settings_callback = callback;
}

void ConversationListScreen::set_announces_callback(AnnouncesCallback callback) {
    _announces_callback = callback;
}

void ConversationListScreen::set_status_callback(StatusCallback callback) {
    _status_callback = callback;
}

void ConversationListScreen::show() {
    lv_obj_clear_flag(_screen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(_screen);  // Bring to front for touch events
}

void ConversationListScreen::hide() {
    lv_obj_add_flag(_screen, LV_OBJ_FLAG_HIDDEN);
}

lv_obj_t* ConversationListScreen::get_object() {
    return _screen;
}

void ConversationListScreen::update_status() {
    // Update WiFi RSSI
    if (WiFi.status() == WL_CONNECTED) {
        int rssi = WiFi.RSSI();
        String wifi_text = String(LV_SYMBOL_WIFI) + " " + String(rssi);
        lv_label_set_text(_label_wifi, wifi_text.c_str());

        // Color based on signal strength
        if (rssi > -50) {
            lv_obj_set_style_text_color(_label_wifi, lv_color_hex(0x4CAF50), 0);  // Green
        } else if (rssi > -70) {
            lv_obj_set_style_text_color(_label_wifi, lv_color_hex(0xFFEB3B), 0);  // Yellow
        } else {
            lv_obj_set_style_text_color(_label_wifi, lv_color_hex(0xF44336), 0);  // Red
        }
    } else {
        lv_label_set_text(_label_wifi, LV_SYMBOL_WIFI " --");
        lv_obj_set_style_text_color(_label_wifi, lv_color_hex(0x808080), 0);
    }

    // Update battery level (read from ADC)
    int raw_adc = analogRead(Pin::BATTERY_ADC);
    float voltage = (raw_adc / 4095.0) * 3.3 * Power::BATTERY_VOLTAGE_DIVIDER;
    int percent = (int)((voltage - Power::BATTERY_EMPTY) / (Power::BATTERY_FULL - Power::BATTERY_EMPTY) * 100);
    percent = constrain(percent, 0, 100);

    String battery_text = String(LV_SYMBOL_BATTERY_FULL) + " " + String(percent) + "%";
    lv_label_set_text(_label_battery, battery_text.c_str());

    // Color based on battery level
    if (percent > 50) {
        lv_obj_set_style_text_color(_label_battery, lv_color_hex(0x4CAF50), 0);  // Green
    } else if (percent > 20) {
        lv_obj_set_style_text_color(_label_battery, lv_color_hex(0xFFEB3B), 0);  // Yellow
    } else {
        lv_obj_set_style_text_color(_label_battery, lv_color_hex(0xF44336), 0);  // Red
    }
}

void ConversationListScreen::on_conversation_clicked(lv_event_t* event) {
    ConversationListScreen* screen = (ConversationListScreen*)lv_event_get_user_data(event);
    lv_obj_t* target = lv_event_get_target(event);

    Bytes* peer_hash = (Bytes*)lv_obj_get_user_data(target);

    if (peer_hash && screen->_conversation_selected_callback) {
        screen->_conversation_selected_callback(*peer_hash);
    }
}

void ConversationListScreen::on_new_message_clicked(lv_event_t* event) {
    ConversationListScreen* screen = (ConversationListScreen*)lv_event_get_user_data(event);

    if (screen->_new_message_callback) {
        screen->_new_message_callback();
    }
}

void ConversationListScreen::on_settings_clicked(lv_event_t* event) {
    ConversationListScreen* screen = (ConversationListScreen*)lv_event_get_user_data(event);

    if (screen->_settings_callback) {
        screen->_settings_callback();
    }
}

void ConversationListScreen::on_bottom_nav_clicked(lv_event_t* event) {
    ConversationListScreen* screen = (ConversationListScreen*)lv_event_get_user_data(event);
    lv_obj_t* target = lv_event_get_target(event);
    int btn_index = (int)(intptr_t)lv_obj_get_user_data(target);

    switch (btn_index) {
        case 0: // Messages - already here
            break;
        case 1: // Announces
            if (screen->_announces_callback) {
                screen->_announces_callback();
            }
            break;
        case 2: // Status
            if (screen->_status_callback) {
                screen->_status_callback();
            }
            break;
        case 3: // Settings - not implemented
        default: {
            const char* btn_names[] = {"Messages", "Announces", "Status", "Settings"};
            static const char* close_btn[] = {"OK", ""};
            lv_obj_t* mbox = lv_msgbox_create(NULL, btn_names[btn_index],
                "Not implemented yet", close_btn, false);
            lv_obj_center(mbox);
            lv_obj_add_event_cb(mbox, msgbox_close_cb, LV_EVENT_VALUE_CHANGED, NULL);
            break;
        }
    }
}

void ConversationListScreen::msgbox_close_cb(lv_event_t* event) {
    lv_obj_t* mbox = lv_event_get_current_target(event);
    lv_msgbox_close(mbox);
}

void ConversationListScreen::on_conversation_long_pressed(lv_event_t* event) {
    ConversationListScreen* screen = (ConversationListScreen*)lv_event_get_user_data(event);
    lv_obj_t* target = lv_event_get_target(event);

    Bytes* peer_hash = (Bytes*)lv_obj_get_user_data(target);
    if (!peer_hash) {
        return;
    }

    // Store the hash we want to delete
    screen->_pending_delete_hash = *peer_hash;

    // Show confirmation dialog
    static const char* btns[] = {"Delete", "Cancel", ""};
    lv_obj_t* mbox = lv_msgbox_create(NULL, "Delete Conversation",
        "Delete this conversation and all messages?", btns, false);
    lv_obj_center(mbox);
    lv_obj_add_event_cb(mbox, on_delete_confirmed, LV_EVENT_VALUE_CHANGED, screen);
}

void ConversationListScreen::on_delete_confirmed(lv_event_t* event) {
    lv_obj_t* mbox = lv_event_get_current_target(event);
    ConversationListScreen* screen = (ConversationListScreen*)lv_event_get_user_data(event);
    uint16_t btn_id = lv_msgbox_get_active_btn(mbox);

    if (btn_id == 0 && screen->_message_store) {  // "Delete" button
        // Delete the conversation
        screen->_message_store->delete_conversation(screen->_pending_delete_hash);
        INFO("Deleted conversation");

        // Refresh the list
        screen->refresh();
    }

    lv_msgbox_close(mbox);
}

String ConversationListScreen::format_timestamp(uint32_t timestamp) {
    uint32_t now = millis() / 1000;  // Convert to seconds
    uint32_t diff = now - timestamp;

    if (diff < 60) {
        return "Just now";
    } else if (diff < 3600) {
        return String(diff / 60) + "m ago";
    } else if (diff < 86400) {
        return String(diff / 3600) + "h ago";
    } else if (diff < 604800) {
        return String(diff / 86400) + "d ago";
    } else {
        return String(diff / 604800) + "w ago";
    }
}

String ConversationListScreen::truncate_hash(const Bytes& hash) {
    return String(hash.toHex().c_str());
}

String ConversationListScreen::parse_display_name(const Bytes& app_data) {
    if (app_data.size() == 0) {
        return String();
    }

    uint8_t first_byte = app_data.data()[0];

    // Check for msgpack array format (LXMF 0.5.0+)
    // fixarray: 0x90-0x9f (array with 0-15 elements)
    // array16: 0xdc
    if ((first_byte >= 0x90 && first_byte <= 0x9f) || first_byte == 0xdc) {
        // Msgpack encoded: [display_name, stamp_cost, ...]
        MsgPack::Unpacker unpacker;
        unpacker.feed(app_data.data(), app_data.size());

        // Get array size
        MsgPack::arr_size_t arr_size;
        if (!unpacker.deserialize(arr_size)) {
            return String();
        }

        if (arr_size.size() < 1) {
            return String();
        }

        // First element is display_name (can be nil or bytes)
        if (unpacker.isNil()) {
            unpacker.unpackNil();
            return String();
        }

        MsgPack::bin_t<uint8_t> name_bin;
        if (unpacker.deserialize(name_bin)) {
            // Convert bytes to string
            return String((const char*)name_bin.data(), name_bin.size());
        }

        return String();
    } else {
        // Original format: raw UTF-8 string
        return String(app_data.toString().c_str());
    }
}

} // namespace LXMF
} // namespace UI

#endif // ARDUINO
