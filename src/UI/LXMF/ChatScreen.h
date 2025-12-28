// Copyright (c) 2024 microReticulum contributors
// SPDX-License-Identifier: MIT

#ifndef UI_LXMF_CHATSCREEN_H
#define UI_LXMF_CHATSCREEN_H

#ifdef ARDUINO
#include <Arduino.h>
#include <lvgl.h>
#include <vector>
#include <functional>
#include "../../Bytes.h"
#include "../../LXMF/LXMessage.h"
#include "../../LXMF/MessageStore.h"

namespace UI {
namespace LXMF {

/**
 * Chat Screen
 *
 * Shows messages in a conversation with:
 * - Scrollable message list
 * - Message bubbles (incoming/outgoing styled differently)
 * - Delivery status indicators (✓ sent, ✓✓ delivered)
 * - Message input area
 * - Send button
 *
 * Layout:
 * ┌─────────────────────────────────────┐
 * │ ← Alice (a1b2c3d4...)     [i]      │ 32px Header
 * ├─────────────────────────────────────┤
 * │                      [Hey there!]   │ Outgoing (right)
 * │                      [10:23 AM ✓]   │
 * │ [How are you doing?]                │ Incoming (left)
 * │ [10:25 AM]                          │ 156px scrollable
 * │             [I'm good, thanks!]     │
 * │             [10:26 AM ✓✓]           │
 * ├─────────────────────────────────────┤
 * │ [Type message...      ]   [Send]    │ 52px Input area
 * └─────────────────────────────────────┘
 */
class ChatScreen {
public:
    /**
     * Message item data
     */
    struct MessageItem {
        RNS::Bytes message_hash;
        String content;
        String timestamp_str;
        bool outgoing;      // true if sent by us
        bool delivered;     // true if delivery confirmed
        bool failed;        // true if delivery failed
    };

    /**
     * Callback types
     */
    using BackCallback = std::function<void()>;
    using SendMessageCallback = std::function<void(const String& content)>;
    using InfoCallback = std::function<void(const RNS::Bytes& peer_hash)>;

    /**
     * Create chat screen
     * @param parent Parent LVGL object (usually lv_scr_act())
     */
    ChatScreen(lv_obj_t* parent = nullptr);

    /**
     * Destructor
     */
    ~ChatScreen();

    /**
     * Load conversation with a specific peer
     * @param peer_hash Peer destination hash
     * @param store Message store to load from
     */
    void load_conversation(const RNS::Bytes& peer_hash, ::LXMF::MessageStore& store);

    /**
     * Add a new message to the chat
     * @param message LXMF message to add
     * @param outgoing true if message is outgoing
     */
    void add_message(const ::LXMF::LXMessage& message, bool outgoing);

    /**
     * Update delivery status of a message
     * @param message_hash Hash of message to update
     * @param delivered true if delivered, false if failed
     */
    void update_message_status(const RNS::Bytes& message_hash, bool delivered);

    /**
     * Refresh message list (reload from store)
     */
    void refresh();

    /**
     * Set callback for back button
     * @param callback Function to call when back button is pressed
     */
    void set_back_callback(BackCallback callback);

    /**
     * Set callback for sending messages
     * @param callback Function to call when send button is pressed
     */
    void set_send_message_callback(SendMessageCallback callback);

    /**
     * Set callback for info button
     * @param callback Function to call when info button is pressed
     */
    void set_info_callback(InfoCallback callback);

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
     * @return Root object
     */
    lv_obj_t* get_object();

private:
    lv_obj_t* _screen;
    lv_obj_t* _header;
    lv_obj_t* _message_list;
    lv_obj_t* _input_area;
    lv_obj_t* _text_area;
    lv_obj_t* _btn_send;
    lv_obj_t* _btn_back;
    lv_obj_t* _btn_info;
    lv_obj_t* _keyboard;

    RNS::Bytes _peer_hash;
    ::LXMF::MessageStore* _message_store;
    std::vector<MessageItem> _messages;

    BackCallback _back_callback;
    SendMessageCallback _send_message_callback;
    InfoCallback _info_callback;

    // UI construction
    void create_header();
    void create_message_list();
    void create_input_area();
    void create_keyboard();
    void create_message_bubble(const MessageItem& item);

    // Event handlers
    static void on_back_clicked(lv_event_t* event);
    static void on_send_clicked(lv_event_t* event);
    static void on_info_clicked(lv_event_t* event);
    static void on_textarea_focused(lv_event_t* event);
    static void on_keyboard_ready(lv_event_t* event);

    // Utility
    static String format_timestamp(double timestamp);
    static String get_delivery_indicator(bool outgoing, bool delivered, bool failed);
};

} // namespace LXMF
} // namespace UI

#endif // ARDUINO
#endif // UI_LXMF_CHATSCREEN_H
