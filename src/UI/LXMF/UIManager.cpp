// Copyright (c) 2024 microReticulum contributors
// SPDX-License-Identifier: MIT

#include "UIManager.h"

#ifdef ARDUINO

#include <lvgl.h>
#include "../../Log.h"

using namespace RNS;

namespace UI {
namespace LXMF {

UIManager::UIManager(Reticulum& reticulum, ::LXMF::LXMRouter& router, ::LXMF::MessageStore& store)
    : _reticulum(reticulum), _router(router), _store(store),
      _current_screen(SCREEN_CONVERSATION_LIST),
      _conversation_list_screen(nullptr),
      _chat_screen(nullptr),
      _compose_screen(nullptr),
      _initialized(false) {
}

UIManager::~UIManager() {
    if (_conversation_list_screen) {
        delete _conversation_list_screen;
    }
    if (_chat_screen) {
        delete _chat_screen;
    }
    if (_compose_screen) {
        delete _compose_screen;
    }
}

bool UIManager::init() {
    if (_initialized) {
        return true;
    }

    INFO("Initializing UIManager");

    // Create all screens
    _conversation_list_screen = new ConversationListScreen();
    _chat_screen = new ChatScreen();
    _compose_screen = new ComposeScreen();

    // Set up callbacks for conversation list screen
    _conversation_list_screen->set_conversation_selected_callback(
        [this](const Bytes& peer_hash) { on_conversation_selected(peer_hash); }
    );

    _conversation_list_screen->set_new_message_callback(
        [this]() { on_new_message(); }
    );

    _conversation_list_screen->set_settings_callback(
        [this]() { on_settings(); }
    );

    // Set up callbacks for chat screen
    _chat_screen->set_back_callback(
        [this]() { on_back_to_conversation_list(); }
    );

    _chat_screen->set_send_message_callback(
        [this](const String& content) { on_send_message_from_chat(content); }
    );

    _chat_screen->set_info_callback(
        [this](const Bytes& peer_hash) { on_info(peer_hash); }
    );

    // Set up callbacks for compose screen
    _compose_screen->set_cancel_callback(
        [this]() { on_cancel_compose(); }
    );

    _compose_screen->set_send_callback(
        [this](const Bytes& dest_hash, const String& message) {
            on_send_message_from_compose(dest_hash, message);
        }
    );

    // Register LXMF delivery callback
    _router.register_delivery_callback(
        [this](::LXMF::LXMessage& message) { on_message_received(message); }
    );

    // Load conversations and show conversation list
    _conversation_list_screen->load_conversations(_store);
    show_conversation_list();

    _initialized = true;
    INFO("UIManager initialized");

    return true;
}

void UIManager::update() {
    // Process outbound LXMF messages
    _router.process_outbound();

    // Process inbound LXMF messages
    _router.process_inbound();
}

void UIManager::show_conversation_list() {
    INFO("Showing conversation list");

    _conversation_list_screen->refresh();
    _conversation_list_screen->show();
    _chat_screen->hide();
    _compose_screen->hide();

    _current_screen = SCREEN_CONVERSATION_LIST;
}

void UIManager::show_chat(const Bytes& peer_hash) {
    std::string hash_hex = peer_hash.toHex().substr(0, 8);
    std::string msg = "Showing chat with peer " + hash_hex + "...";
    INFO(msg.c_str());

    _current_peer_hash = peer_hash;

    _chat_screen->load_conversation(peer_hash, _store);
    _chat_screen->show();
    _conversation_list_screen->hide();
    _compose_screen->hide();

    _current_screen = SCREEN_CHAT;
}

void UIManager::show_compose() {
    INFO("Showing compose screen");

    _compose_screen->clear();
    _compose_screen->show();
    _conversation_list_screen->hide();
    _chat_screen->hide();

    _current_screen = SCREEN_COMPOSE;
}

void UIManager::on_conversation_selected(const Bytes& peer_hash) {
    show_chat(peer_hash);
}

void UIManager::on_new_message() {
    show_compose();
}

void UIManager::on_settings() {
    INFO("Settings button clicked");

    // Show settings info message box with OK button
    static const char* btns[] = {"OK", ""};
    lv_obj_t* mbox = lv_msgbox_create(NULL, "Settings",
        "Settings not implemented yet.", btns, false);
    lv_obj_center(mbox);
    lv_obj_add_event_cb(mbox, settings_msgbox_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

void UIManager::settings_msgbox_cb(lv_event_t* event) {
    lv_obj_t* mbox = lv_event_get_current_target(event);
    lv_msgbox_close(mbox);
}

void UIManager::on_back_to_conversation_list() {
    show_conversation_list();
}

void UIManager::on_send_message_from_chat(const String& content) {
    send_message(_current_peer_hash, content);
}

void UIManager::on_send_message_from_compose(const Bytes& dest_hash, const String& message) {
    send_message(dest_hash, message);

    // Switch to chat screen for this conversation
    show_chat(dest_hash);
}

void UIManager::on_cancel_compose() {
    show_conversation_list();
}

void UIManager::on_info(const Bytes& peer_hash) {
    std::string hash_hex = peer_hash.toHex().substr(0, 8);
    std::string msg = "Info button clicked for peer " + hash_hex + "...";
    INFO(msg.c_str());
    // TODO: Implement peer info screen
}

void UIManager::send_message(const Bytes& dest_hash, const String& content) {
    std::string hash_hex = dest_hash.toHex().substr(0, 8);
    std::string msg = "Sending message to " + hash_hex + "...";
    INFO(msg.c_str());

    // Look up destination (or create placeholder)
    // In a real implementation, we'd need to recall the destination identity
    Identity dest_identity = Identity::recall(dest_hash);

    Destination destination(Type::NONE);
    if (dest_identity) {
        destination = Destination(dest_identity, Type::Destination::OUT, Type::Destination::SINGLE, "lxmf", "delivery");
    } else {
        WARNING("  Destination identity not known, message may fail");
        // Create a placeholder destination with just the hash
        // This will work if the peer announces later
    }

    // Get our source destination
    Destination source = _router.delivery_destination();

    // Create message
    Bytes content_bytes((const uint8_t*)content.c_str(), content.length());
    Bytes title;  // Empty title

    ::LXMF::LXMessage message(destination, source, content_bytes, title);

    // Add to UI immediately (optimistic update)
    if (_current_screen == SCREEN_CHAT && _current_peer_hash == dest_hash) {
        _chat_screen->add_message(message, true);
    }

    // Save to store
    _store.save_message(message);

    // Queue for sending
    _router.handle_outbound(message);

    INFO("  Message queued for delivery");
}

void UIManager::on_message_received(::LXMF::LXMessage& message) {
    std::string source_hex = message.source_hash().toHex().substr(0, 8);
    std::string msg = "Message received from " + source_hex + "...";
    INFO(msg.c_str());

    // Save to store
    _store.save_message(message);

    // Update UI if we're viewing this conversation
    if (_current_screen == SCREEN_CHAT && _current_peer_hash == message.source_hash()) {
        _chat_screen->add_message(message, false);
    }

    // Update conversation list unread count
    // TODO: Track unread counts
    _conversation_list_screen->refresh();

    INFO("  Message processed");
}

void UIManager::on_message_delivered(::LXMF::LXMessage& message) {
    std::string hash_hex = message.hash().toHex().substr(0, 8);
    std::string msg = "Message delivered: " + hash_hex + "...";
    INFO(msg.c_str());

    // Update UI if we're viewing this conversation
    if (_current_screen == SCREEN_CHAT && _current_peer_hash == message.destination_hash()) {
        _chat_screen->update_message_status(message.hash(), true);
    }
}

void UIManager::on_message_failed(::LXMF::LXMessage& message) {
    std::string hash_hex = message.hash().toHex().substr(0, 8);
    std::string msg = "Message delivery failed: " + hash_hex + "...";
    WARNING(msg.c_str());

    // Update UI if we're viewing this conversation
    if (_current_screen == SCREEN_CHAT && _current_peer_hash == message.destination_hash()) {
        _chat_screen->update_message_status(message.hash(), false);
    }
}

void UIManager::refresh_current_screen() {
    switch (_current_screen) {
        case SCREEN_CONVERSATION_LIST:
            _conversation_list_screen->refresh();
            break;
        case SCREEN_CHAT:
            _chat_screen->refresh();
            break;
        case SCREEN_COMPOSE:
            // No refresh needed
            break;
    }
}

} // namespace LXMF
} // namespace UI

#endif // ARDUINO
