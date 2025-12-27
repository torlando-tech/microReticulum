// Copyright (c) 2024 microReticulum contributors
// SPDX-License-Identifier: MIT

#ifndef UI_LXMF_CONVERSATIONLISTSCREEN_H
#define UI_LXMF_CONVERSATIONLISTSCREEN_H

#ifdef ARDUINO
#include <Arduino.h>
#include <lvgl.h>
#include <vector>
#include <functional>
#include "../../Bytes.h"
#include "../../LXMF/MessageStore.h"

namespace UI {
namespace LXMF {

/**
 * Conversation List Screen
 *
 * Shows a scrollable list of all LXMF conversations with:
 * - Peer name/hash (truncated)
 * - Last message preview
 * - Timestamp
 * - Unread count indicator
 * - Navigation buttons (New message, Settings)
 *
 * Layout:
 * ┌─────────────────────────────────────┐
 * │ LXMF Messages          [New] [☰]   │ 32px header
 * ├─────────────────────────────────────┤
 * │ ┌─ Alice (a1b2c3...)              │
 * │ │   Hey, how are you?              │
 * │ │   2 hours ago          [2]       │ Unread count
 * │ └─                                  │
 * │ ┌─ Bob (d4e5f6...)                │ 176px scrollable
 * │ │   See you tomorrow!              │
 * │ │   Yesterday                      │
 * │ └─                                  │
 * ├─────────────────────────────────────┤
 * │  [💬] [👤] [📡] [⚙️]                │ 32px bottom nav
 * └─────────────────────────────────────┘
 */
class ConversationListScreen {
public:
    /**
     * Conversation item data
     */
    struct ConversationItem {
        RNS::Bytes peer_hash;
        String peer_name;      // Or truncated hash if no name
        String last_message;   // Preview of last message
        String timestamp_str;  // Human-readable time
        uint32_t timestamp;    // Unix timestamp
        uint16_t unread_count;
    };

    /**
     * Callback types
     */
    using ConversationSelectedCallback = std::function<void(const RNS::Bytes& peer_hash)>;
    using NewMessageCallback = std::function<void()>;
    using SettingsCallback = std::function<void()>;

    /**
     * Create conversation list screen
     * @param parent Parent LVGL object (usually lv_scr_act())
     */
    ConversationListScreen(lv_obj_t* parent = nullptr);

    /**
     * Destructor
     */
    ~ConversationListScreen();

    /**
     * Load conversations from message store
     * @param store Message store to load from
     */
    void load_conversations(::LXMF::MessageStore& store);

    /**
     * Refresh conversation list (reload from store)
     */
    void refresh();

    /**
     * Update unread count for a specific conversation
     * @param peer_hash Peer hash
     * @param unread_count New unread count
     */
    void update_unread_count(const RNS::Bytes& peer_hash, uint16_t unread_count);

    /**
     * Set callback for conversation selection
     * @param callback Function to call when conversation is selected
     */
    void set_conversation_selected_callback(ConversationSelectedCallback callback);

    /**
     * Set callback for new message button
     * @param callback Function to call when new message button is pressed
     */
    void set_new_message_callback(NewMessageCallback callback);

    /**
     * Set callback for settings button
     * @param callback Function to call when settings button is pressed
     */
    void set_settings_callback(SettingsCallback callback);

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
    lv_obj_t* _list;
    lv_obj_t* _bottom_nav;
    lv_obj_t* _btn_new;
    lv_obj_t* _btn_settings;

    ::LXMF::MessageStore* _message_store;
    std::vector<ConversationItem> _conversations;

    ConversationSelectedCallback _conversation_selected_callback;
    NewMessageCallback _new_message_callback;
    SettingsCallback _settings_callback;

    // UI construction
    void create_header();
    void create_list();
    void create_bottom_nav();
    void create_conversation_item(const ConversationItem& item);

    // Event handlers
    static void on_conversation_clicked(lv_event_t* event);
    static void on_new_message_clicked(lv_event_t* event);
    static void on_settings_clicked(lv_event_t* event);
    static void on_bottom_nav_clicked(lv_event_t* event);
    static void msgbox_close_cb(lv_event_t* event);

    // Utility
    static String format_timestamp(uint32_t timestamp);
    static String truncate_hash(const RNS::Bytes& hash);
};

} // namespace LXMF
} // namespace UI

#endif // ARDUINO
#endif // UI_LXMF_CONVERSATIONLISTSCREEN_H
