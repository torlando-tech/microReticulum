// Copyright (c) 2024 microReticulum contributors
// SPDX-License-Identifier: MIT

#include "ChatScreen.h"

#ifdef ARDUINO

#include "../../Log.h"
#include "../LVGL/LVGLInit.h"

using namespace RNS;

namespace UI {
namespace LXMF {

ChatScreen::ChatScreen(lv_obj_t* parent)
    : _screen(nullptr), _header(nullptr), _message_list(nullptr), _input_area(nullptr),
      _text_area(nullptr), _btn_send(nullptr), _btn_back(nullptr), _btn_info(nullptr),
      _keyboard(nullptr), _message_store(nullptr) {

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
    create_message_list();
    create_input_area();
    create_keyboard();

    // Hide by default
    hide();

    TRACE("ChatScreen created");
}

ChatScreen::~ChatScreen() {
    if (_screen) {
        lv_obj_del(_screen);
    }
}

void ChatScreen::create_header() {
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

    // Peer name/hash (will be set when conversation is loaded)
    lv_obj_t* label_peer = lv_label_create(_header);
    lv_label_set_text(label_peer, "Chat");
    lv_obj_align(label_peer, LV_ALIGN_LEFT_MID, 60, 0);
    lv_obj_set_style_text_color(label_peer, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(label_peer, &lv_font_montserrat_16, 0);

    // Info button
    _btn_info = lv_btn_create(_header);
    lv_obj_set_size(_btn_info, 40, 28);
    lv_obj_align(_btn_info, LV_ALIGN_RIGHT_MID, -2, 0);
    lv_obj_set_style_bg_color(_btn_info, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_color(_btn_info, lv_color_hex(0x444444), LV_STATE_PRESSED);
    lv_obj_add_event_cb(_btn_info, on_info_clicked, LV_EVENT_CLICKED, this);

    lv_obj_t* label_info = lv_label_create(_btn_info);
    lv_label_set_text(label_info, LV_SYMBOL_EYE_OPEN);
    lv_obj_center(label_info);
    lv_obj_set_style_text_color(label_info, lv_color_hex(0xe0e0e0), 0);
}

void ChatScreen::create_message_list() {
    _message_list = lv_obj_create(_screen);
    lv_obj_set_size(_message_list, LV_PCT(100), 152);  // 240 - 36 (header) - 52 (input)
    lv_obj_align(_message_list, LV_ALIGN_TOP_MID, 0, 36);
    lv_obj_set_style_pad_all(_message_list, 4, 0);
    lv_obj_set_style_pad_gap(_message_list, 4, 0);
    lv_obj_set_style_bg_color(_message_list, lv_color_hex(0x0d0d0d), 0);  // Slightly darker
    lv_obj_set_style_border_width(_message_list, 0, 0);
    lv_obj_set_style_radius(_message_list, 0, 0);
    lv_obj_set_flex_flow(_message_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_message_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
}

void ChatScreen::create_input_area() {
    _input_area = lv_obj_create(_screen);
    lv_obj_set_size(_input_area, LV_PCT(100), 52);
    lv_obj_align(_input_area, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(_input_area, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_width(_input_area, 0, 0);
    lv_obj_set_style_radius(_input_area, 0, 0);
    lv_obj_set_style_pad_all(_input_area, 0, 0);
    lv_obj_clear_flag(_input_area, LV_OBJ_FLAG_SCROLLABLE);

    // Text area for message input
    _text_area = lv_textarea_create(_input_area);
    lv_obj_set_size(_text_area, 245, 40);
    lv_obj_align(_text_area, LV_ALIGN_LEFT_MID, 4, 0);
    lv_textarea_set_placeholder_text(_text_area, "Type message...");
    lv_textarea_set_one_line(_text_area, false);
    lv_textarea_set_max_length(_text_area, 500);
    lv_obj_set_style_bg_color(_text_area, lv_color_hex(0x2a2a2a), 0);
    lv_obj_set_style_text_color(_text_area, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_border_color(_text_area, lv_color_hex(0x404040), 0);

    // Send button
    _btn_send = lv_btn_create(_input_area);
    lv_obj_set_size(_btn_send, 65, 40);
    lv_obj_align(_btn_send, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_set_style_bg_color(_btn_send, lv_color_hex(0x2e7d32), 0);
    lv_obj_set_style_bg_color(_btn_send, lv_color_hex(0x388e3c), LV_STATE_PRESSED);
    lv_obj_add_event_cb(_btn_send, on_send_clicked, LV_EVENT_CLICKED, this);

    lv_obj_t* label_send = lv_label_create(_btn_send);
    lv_label_set_text(label_send, "Send");
    lv_obj_center(label_send);
    lv_obj_set_style_text_color(label_send, lv_color_hex(0xffffff), 0);

    // Add focus event to show keyboard when text area is tapped
    lv_obj_add_event_cb(_text_area, on_textarea_focused, LV_EVENT_FOCUSED, this);
}

void ChatScreen::create_keyboard() {
    // Create on-screen keyboard (hidden by default)
    _keyboard = lv_keyboard_create(_screen);
    lv_obj_set_size(_keyboard, LV_PCT(100), 120);  // Reduced height for small screen
    lv_obj_align(_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);

    // Style the keyboard for dark theme
    lv_obj_set_style_bg_color(_keyboard, lv_color_hex(0x2a2a2a), 0);
    lv_obj_set_style_bg_color(_keyboard, lv_color_hex(0x404040), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(_keyboard, lv_color_hex(0x505050), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(_keyboard, lv_color_hex(0xffffff), LV_PART_ITEMS);

    // Hide keyboard initially
    lv_obj_add_flag(_keyboard, LV_OBJ_FLAG_HIDDEN);

    // Add ready event to handle Enter key
    lv_obj_add_event_cb(_keyboard, on_keyboard_ready, LV_EVENT_READY, this);

    // Add cancel event to hide keyboard when X button is pressed
    lv_obj_add_event_cb(_keyboard, [](lv_event_t* event) {
        ChatScreen* screen = (ChatScreen*)lv_event_get_user_data(event);
        if (screen->_keyboard) {
            lv_obj_add_flag(screen->_keyboard, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_height(screen->_message_list, 152);
        }
    }, LV_EVENT_CANCEL, this);
}

void ChatScreen::on_textarea_focused(lv_event_t* event) {
    ChatScreen* screen = (ChatScreen*)lv_event_get_user_data(event);

    if (screen->_keyboard) {
        // Show keyboard and associate with text area
        lv_keyboard_set_textarea(screen->_keyboard, screen->_text_area);
        lv_obj_clear_flag(screen->_keyboard, LV_OBJ_FLAG_HIDDEN);

        // Shrink message list to make room for keyboard
        lv_obj_set_height(screen->_message_list, 32);  // Minimal height when keyboard shown
    }
}

void ChatScreen::on_keyboard_ready(lv_event_t* event) {
    ChatScreen* screen = (ChatScreen*)lv_event_get_user_data(event);

    // Enter key was pressed - hide keyboard and send message
    if (screen->_keyboard) {
        lv_obj_add_flag(screen->_keyboard, LV_OBJ_FLAG_HIDDEN);

        // Restore message list height
        lv_obj_set_height(screen->_message_list, 152);
    }

    // Also trigger send
    const char* text = lv_textarea_get_text(screen->_text_area);
    String message(text);

    if (message.length() > 0 && screen->_send_message_callback) {
        screen->_send_message_callback(message);
        lv_textarea_set_text(screen->_text_area, "");
    }
}

void ChatScreen::load_conversation(const Bytes& peer_hash, ::LXMF::MessageStore& store) {
    _peer_hash = peer_hash;
    _message_store = &store;

    INFO(("Loading conversation with peer " + String(peer_hash.toHex().substr(0, 8).c_str()) + "...").c_str());

    // Update header with peer info
    String peer_name = String(peer_hash.toHex().substr(0, 8).c_str()) + "...";
    lv_obj_t* label_peer = lv_obj_get_child(_header, 1);  // Second child is peer label
    lv_label_set_text(label_peer, peer_name.c_str());

    refresh();
}

void ChatScreen::refresh() {
    if (!_message_store) {
        return;
    }

    INFO("Refreshing chat messages");

    // Clear existing messages
    lv_obj_clean(_message_list);
    _messages.clear();

    // Load messages from store
    std::vector<Bytes> message_hashes = _message_store->get_messages_for_conversation(_peer_hash);

    INFO(("  Found " + String(message_hashes.size()) + " messages").c_str());

    for (const auto& msg_hash : message_hashes) {
        ::LXMF::LXMessage msg = _message_store->load_message(msg_hash);

        MessageItem item;
        item.message_hash = msg_hash;

        // Get message content
        String content((const char*)msg.content().data(), msg.content().size());
        item.content = content;

        item.timestamp_str = format_timestamp(msg.timestamp());

        // Determine if outgoing (check if source matches our identity)
        // TODO: Need to pass our identity hash for comparison
        item.outgoing = msg.incoming() == false;

        item.delivered = (msg.state() == ::LXMF::Type::Message::DELIVERED);
        item.failed = (msg.state() == ::LXMF::Type::Message::FAILED);

        _messages.push_back(item);
        create_message_bubble(item);
    }

    // Scroll to bottom
    lv_obj_scroll_to_y(_message_list, LV_COORD_MAX, LV_ANIM_OFF);
}

void ChatScreen::create_message_bubble(const MessageItem& item) {
    // Create container for message bubble
    lv_obj_t* bubble = lv_obj_create(_message_list);
    lv_obj_set_width(bubble, LV_PCT(80));
    lv_obj_set_height(bubble, LV_SIZE_CONTENT);

    // Style based on incoming/outgoing
    if (item.outgoing) {
        // Outgoing: blue, align right
        lv_obj_set_style_bg_color(bubble, lv_color_hex(0x1976D2), 0);
        lv_obj_set_style_align(bubble, LV_ALIGN_TOP_RIGHT, 0);
    } else {
        // Incoming: gray, align left
        lv_obj_set_style_bg_color(bubble, lv_color_hex(0x424242), 0);
        lv_obj_set_style_align(bubble, LV_ALIGN_TOP_LEFT, 0);
    }

    lv_obj_set_style_radius(bubble, 10, 0);
    lv_obj_set_style_pad_all(bubble, 8, 0);
    lv_obj_clear_flag(bubble, LV_OBJ_FLAG_SCROLLABLE);

    // Message content
    lv_obj_t* label_content = lv_label_create(bubble);
    lv_label_set_text(label_content, item.content.c_str());
    lv_label_set_long_mode(label_content, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label_content, LV_PCT(100));
    lv_obj_align(label_content, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_text_color(label_content, lv_color_white(), 0);

    // Timestamp and delivery status
    String status_text = item.timestamp_str + " " + get_delivery_indicator(item.outgoing, item.delivered, item.failed);

    lv_obj_t* label_status = lv_label_create(bubble);
    lv_label_set_text(label_status, status_text.c_str());
    lv_obj_align(label_status, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_text_color(label_status, lv_color_hex(0xB0B0B0), 0);
    lv_obj_set_style_text_font(label_status, &lv_font_montserrat_14, 0);
}

void ChatScreen::add_message(const ::LXMF::LXMessage& message, bool outgoing) {
    MessageItem item;
    item.message_hash = message.hash();

    String content((const char*)message.content().data(), message.content().size());
    item.content = content;
    item.timestamp_str = format_timestamp(message.timestamp());
    item.outgoing = outgoing;
    item.delivered = false;
    item.failed = false;

    _messages.push_back(item);
    create_message_bubble(item);

    // Scroll to bottom
    lv_obj_scroll_to_y(_message_list, LV_COORD_MAX, LV_ANIM_ON);
}

void ChatScreen::update_message_status(const Bytes& message_hash, bool delivered) {
    // Find message and update status
    for (auto& msg : _messages) {
        if (msg.message_hash == message_hash) {
            msg.delivered = delivered;
            msg.failed = !delivered;
            refresh();  // Redraw messages
            break;
        }
    }
}

void ChatScreen::set_back_callback(BackCallback callback) {
    _back_callback = callback;
}

void ChatScreen::set_send_message_callback(SendMessageCallback callback) {
    _send_message_callback = callback;
}

void ChatScreen::set_info_callback(InfoCallback callback) {
    _info_callback = callback;
}

void ChatScreen::show() {
    lv_obj_clear_flag(_screen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(_screen);  // Bring to front for touch events

    // Add text area to default group so keyboard works when tapped
    lv_group_t* group = LVGL::LVGLInit::get_default_group();
    if (group && _text_area) {
        lv_group_add_obj(group, _text_area);
    }
}

void ChatScreen::hide() {
    lv_obj_add_flag(_screen, LV_OBJ_FLAG_HIDDEN);
}

lv_obj_t* ChatScreen::get_object() {
    return _screen;
}

void ChatScreen::on_back_clicked(lv_event_t* event) {
    ChatScreen* screen = (ChatScreen*)lv_event_get_user_data(event);

    if (screen->_back_callback) {
        screen->_back_callback();
    }
}

void ChatScreen::on_send_clicked(lv_event_t* event) {
    ChatScreen* screen = (ChatScreen*)lv_event_get_user_data(event);

    // Get message text
    const char* text = lv_textarea_get_text(screen->_text_area);
    String message(text);

    if (message.length() > 0 && screen->_send_message_callback) {
        screen->_send_message_callback(message);

        // Clear text area
        lv_textarea_set_text(screen->_text_area, "");
    }
}

void ChatScreen::on_info_clicked(lv_event_t* event) {
    ChatScreen* screen = (ChatScreen*)lv_event_get_user_data(event);

    if (screen->_info_callback) {
        screen->_info_callback(screen->_peer_hash);
    }
}

String ChatScreen::format_timestamp(double timestamp) {
    // Convert to time_t for formatting
    time_t time = (time_t)timestamp;
    struct tm* timeinfo = localtime(&time);

    char buffer[32];
    strftime(buffer, sizeof(buffer), "%I:%M %p", timeinfo);

    return String(buffer);
}

String ChatScreen::get_delivery_indicator(bool outgoing, bool delivered, bool failed) {
    if (!outgoing) {
        return "";  // No indicator for incoming messages
    }

    if (failed) {
        return LV_SYMBOL_CLOSE;  // X for failed
    } else if (delivered) {
        return LV_SYMBOL_OK LV_SYMBOL_OK;  // Double check for delivered
    } else {
        return LV_SYMBOL_OK;  // Single check for sent
    }
}

} // namespace LXMF
} // namespace UI

#endif // ARDUINO
