// Copyright (c) 2024 microReticulum contributors
// SPDX-License-Identifier: MIT

#include "ComposeScreen.h"

#ifdef ARDUINO

#include "../../Log.h"

using namespace RNS;

namespace UI {
namespace LXMF {

ComposeScreen::ComposeScreen(lv_obj_t* parent)
    : _screen(nullptr), _header(nullptr), _content_area(nullptr), _button_area(nullptr),
      _text_area_dest(nullptr), _text_area_message(nullptr),
      _btn_cancel(nullptr), _btn_send(nullptr), _btn_back(nullptr) {

    // Create screen object
    if (parent) {
        _screen = lv_obj_create(parent);
    } else {
        _screen = lv_obj_create(lv_scr_act());
    }

    lv_obj_set_size(_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(_screen, lv_color_hex(0x121212), 0);  // Dark background
    lv_obj_set_style_bg_opa(_screen, LV_OPA_COVER, 0);

    // Create UI components
    create_header();
    create_content_area();
    create_button_area();

    // Hide by default
    hide();

    TRACE("ComposeScreen created");
}

ComposeScreen::~ComposeScreen() {
    if (_screen) {
        lv_obj_del(_screen);
    }
}

void ComposeScreen::create_header() {
    _header = lv_obj_create(_screen);
    lv_obj_set_size(_header, LV_PCT(100), 32);
    lv_obj_align(_header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(_header, lv_color_hex(0x1E88E5), 0);

    // Back button
    _btn_back = lv_btn_create(_header);
    lv_obj_set_size(_btn_back, 40, 24);
    lv_obj_align(_btn_back, LV_ALIGN_LEFT_MID, 5, 0);
    lv_obj_set_style_bg_color(_btn_back, lv_color_hex(0x1565C0), 0);  // Darker blue
    lv_obj_add_event_cb(_btn_back, on_back_clicked, LV_EVENT_CLICKED, this);

    lv_obj_t* label_back = lv_label_create(_btn_back);
    lv_label_set_text(label_back, LV_SYMBOL_LEFT);
    lv_obj_center(label_back);
    lv_obj_set_style_text_color(label_back, lv_color_white(), 0);

    // Title
    lv_obj_t* title = lv_label_create(_header);
    lv_label_set_text(title, "New Message");
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 50, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
}

void ComposeScreen::create_content_area() {
    _content_area = lv_obj_create(_screen);
    lv_obj_set_size(_content_area, LV_PCT(100), 156);  // 240 - 32 (header) - 52 (buttons) = 156
    lv_obj_align(_content_area, LV_ALIGN_TOP_MID, 0, 32);
    lv_obj_set_style_pad_all(_content_area, 10, 0);
    lv_obj_clear_flag(_content_area, LV_OBJ_FLAG_SCROLLABLE);

    // "To:" label
    lv_obj_t* label_to = lv_label_create(_content_area);
    lv_label_set_text(label_to, "To:");
    lv_obj_align(label_to, LV_ALIGN_TOP_LEFT, 0, 0);

    // Destination hash input
    _text_area_dest = lv_textarea_create(_content_area);
    lv_obj_set_size(_text_area_dest, LV_PCT(100), 40);
    lv_obj_align(_text_area_dest, LV_ALIGN_TOP_LEFT, 0, 20);
    lv_textarea_set_placeholder_text(_text_area_dest, "Paste destination hash (32 hex chars)");
    lv_textarea_set_one_line(_text_area_dest, true);
    lv_textarea_set_max_length(_text_area_dest, 32);
    lv_textarea_set_accepted_chars(_text_area_dest, "0123456789abcdefABCDEF");

    // "Message:" label
    lv_obj_t* label_message = lv_label_create(_content_area);
    lv_label_set_text(label_message, "Message:");
    lv_obj_align(label_message, LV_ALIGN_TOP_LEFT, 0, 70);

    // Message input
    _text_area_message = lv_textarea_create(_content_area);
    lv_obj_set_size(_text_area_message, LV_PCT(100), 70);
    lv_obj_align(_text_area_message, LV_ALIGN_TOP_LEFT, 0, 90);
    lv_textarea_set_placeholder_text(_text_area_message, "Type your message here...");
    lv_textarea_set_one_line(_text_area_message, false);
    lv_textarea_set_max_length(_text_area_message, 500);
}

void ComposeScreen::create_button_area() {
    _button_area = lv_obj_create(_screen);
    lv_obj_set_size(_button_area, LV_PCT(100), 52);
    lv_obj_align(_button_area, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(_button_area, lv_color_hex(0x212121), 0);
    lv_obj_set_flex_flow(_button_area, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(_button_area, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Cancel button
    _btn_cancel = lv_btn_create(_button_area);
    lv_obj_set_size(_btn_cancel, 100, 40);
    lv_obj_set_style_bg_color(_btn_cancel, lv_color_hex(0x757575), 0);  // Gray
    lv_obj_add_event_cb(_btn_cancel, on_cancel_clicked, LV_EVENT_CLICKED, this);

    lv_obj_t* label_cancel = lv_label_create(_btn_cancel);
    lv_label_set_text(label_cancel, "Cancel");
    lv_obj_center(label_cancel);
    lv_obj_set_style_text_color(label_cancel, lv_color_white(), 0);

    // Spacer
    lv_obj_t* spacer = lv_obj_create(_button_area);
    lv_obj_set_size(spacer, 20, 1);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);

    // Send button
    _btn_send = lv_btn_create(_button_area);
    lv_obj_set_size(_btn_send, 100, 40);
    lv_obj_add_event_cb(_btn_send, on_send_clicked, LV_EVENT_CLICKED, this);
    lv_obj_set_style_bg_color(_btn_send, lv_color_hex(0x4CAF50), 0);  // Green

    lv_obj_t* label_send = lv_label_create(_btn_send);
    lv_label_set_text(label_send, "Send");
    lv_obj_center(label_send);
    lv_obj_set_style_text_color(label_send, lv_color_white(), 0);
}

void ComposeScreen::clear() {
    lv_textarea_set_text(_text_area_dest, "");
    lv_textarea_set_text(_text_area_message, "");
}

void ComposeScreen::set_destination(const Bytes& dest_hash) {
    String hash_str = dest_hash.toHex().c_str();
    lv_textarea_set_text(_text_area_dest, hash_str.c_str());
}

void ComposeScreen::set_cancel_callback(CancelCallback callback) {
    _cancel_callback = callback;
}

void ComposeScreen::set_send_callback(SendCallback callback) {
    _send_callback = callback;
}

void ComposeScreen::show() {
    lv_obj_clear_flag(_screen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(_screen);  // Bring to front for touch events
}

void ComposeScreen::hide() {
    lv_obj_add_flag(_screen, LV_OBJ_FLAG_HIDDEN);
}

lv_obj_t* ComposeScreen::get_object() {
    return _screen;
}

void ComposeScreen::on_back_clicked(lv_event_t* event) {
    ComposeScreen* screen = (ComposeScreen*)lv_event_get_user_data(event);

    if (screen->_cancel_callback) {
        screen->_cancel_callback();
    }
}

void ComposeScreen::on_cancel_clicked(lv_event_t* event) {
    ComposeScreen* screen = (ComposeScreen*)lv_event_get_user_data(event);

    if (screen->_cancel_callback) {
        screen->_cancel_callback();
    }
}

void ComposeScreen::on_send_clicked(lv_event_t* event) {
    ComposeScreen* screen = (ComposeScreen*)lv_event_get_user_data(event);

    // Get destination hash
    const char* dest_text = lv_textarea_get_text(screen->_text_area_dest);
    String dest_hash_str(dest_text);
    dest_hash_str.trim();
    dest_hash_str.toLowerCase();

    // Validate destination hash
    if (!screen->validate_destination_hash(dest_hash_str)) {
        ERROR(("Invalid destination hash: " + dest_hash_str).c_str());
        // TODO: Show error message to user
        return;
    }

    // Get message text
    const char* message_text = lv_textarea_get_text(screen->_text_area_message);
    String message(message_text);
    message.trim();

    if (message.length() == 0) {
        ERROR("Message is empty");
        // TODO: Show error message to user
        return;
    }

    // Convert hex string to bytes
    Bytes dest_hash;
    dest_hash.assignHex(dest_hash_str.c_str());

    if (screen->_send_callback) {
        screen->_send_callback(dest_hash, message);
    }

    // Clear form
    screen->clear();
}

bool ComposeScreen::validate_destination_hash(const String& hash_str) {
    // Must be exactly 32 hex characters (16 bytes)
    if (hash_str.length() != 32) {
        return false;
    }

    // Check all characters are valid hex
    for (size_t i = 0; i < hash_str.length(); i++) {
        char c = hash_str.charAt(i);
        if (!isxdigit(c)) {
            return false;
        }
    }

    return true;
}

} // namespace LXMF
} // namespace UI

#endif // ARDUINO
