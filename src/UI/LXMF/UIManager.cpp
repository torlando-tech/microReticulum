// Copyright (c) 2024 microReticulum contributors
// SPDX-License-Identifier: MIT

#include "UIManager.h"

#ifdef ARDUINO

#include <lvgl.h>
#include "../../Log.h"
#include "tone/Tone.h"

using namespace RNS;

namespace UI {
namespace LXMF {

UIManager::UIManager(Reticulum& reticulum, ::LXMF::LXMRouter& router, ::LXMF::MessageStore& store)
    : _reticulum(reticulum), _router(router), _store(store),
      _current_screen(SCREEN_CONVERSATION_LIST),
      _conversation_list_screen(nullptr),
      _chat_screen(nullptr),
      _compose_screen(nullptr),
      _announce_list_screen(nullptr),
      _status_screen(nullptr),
      _qr_screen(nullptr),
      _settings_screen(nullptr),
      _propagation_nodes_screen(nullptr),
      _propagation_manager(nullptr),
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
    if (_announce_list_screen) {
        delete _announce_list_screen;
    }
    if (_status_screen) {
        delete _status_screen;
    }
    if (_qr_screen) {
        delete _qr_screen;
    }
    if (_settings_screen) {
        delete _settings_screen;
    }
    if (_propagation_nodes_screen) {
        delete _propagation_nodes_screen;
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
    _announce_list_screen = new AnnounceListScreen();
    _status_screen = new StatusScreen();
    _qr_screen = new QRScreen();
    _settings_screen = new SettingsScreen();
    _propagation_nodes_screen = new PropagationNodesScreen();

    // Set up callbacks for conversation list screen
    _conversation_list_screen->set_conversation_selected_callback(
        [this](const Bytes& peer_hash) { on_conversation_selected(peer_hash); }
    );

    _conversation_list_screen->set_new_message_callback(
        [this]() { on_new_message(); }
    );

    _conversation_list_screen->set_settings_callback(
        [this]() { show_settings(); }
    );

    _conversation_list_screen->set_announces_callback(
        [this]() { show_announces(); }
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

    // Set up callbacks for announce list screen
    _announce_list_screen->set_announce_selected_callback(
        [this](const Bytes& dest_hash) { on_announce_selected(dest_hash); }
    );

    _announce_list_screen->set_back_callback(
        [this]() { on_back_from_announces(); }
    );

    _announce_list_screen->set_send_announce_callback(
        [this]() {
            INFO("Sending LXMF announce");
            _router.announce();
        }
    );

    // Set up callbacks for status screen
    _status_screen->set_back_callback(
        [this]() { on_back_from_status(); }
    );

    _status_screen->set_share_callback(
        [this]() { on_share_from_status(); }
    );

    // Set up callbacks for QR screen
    _qr_screen->set_back_callback(
        [this]() { on_back_from_qr(); }
    );

    // Set up callbacks for settings screen
    _settings_screen->set_back_callback(
        [this]() { on_back_from_settings(); }
    );

    _settings_screen->set_propagation_nodes_callback(
        [this]() { show_propagation_nodes(); }
    );

    // Set up callbacks for propagation nodes screen
    _propagation_nodes_screen->set_back_callback(
        [this]() { on_back_from_propagation_nodes(); }
    );

    _propagation_nodes_screen->set_node_selected_callback(
        [this](const Bytes& node_hash) { on_propagation_node_selected(node_hash); }
    );

    _propagation_nodes_screen->set_auto_select_changed_callback(
        [this](bool enabled) { on_propagation_auto_select_changed(enabled); }
    );

    _propagation_nodes_screen->set_sync_callback(
        [this]() { on_propagation_sync(); }
    );

    // Load settings from NVS
    _settings_screen->load_settings();

    // Set identity and LXMF address on settings screen
    _settings_screen->set_identity_hash(_router.identity().hash());
    _settings_screen->set_lxmf_address(_router.delivery_destination().hash());

    // Set up callback for status button in conversation list
    _conversation_list_screen->set_status_callback(
        [this]() { show_status(); }
    );

    // Set identity hash and LXMF address on status screen
    _status_screen->set_identity_hash(_router.identity().hash());
    _status_screen->set_lxmf_address(_router.delivery_destination().hash());

    // Set identity and LXMF address on QR screen
    _qr_screen->set_identity(_router.identity());
    _qr_screen->set_lxmf_address(_router.delivery_destination().hash());

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

    // Update status indicators (WiFi/battery) on conversation list
    static uint32_t last_status_update = 0;
    uint32_t now = millis();
    if (now - last_status_update > 3000) {  // Update every 3 seconds
        last_status_update = now;
        if (_conversation_list_screen) {
            _conversation_list_screen->update_status();
        }
        // Update status screen if visible
        if (_current_screen == SCREEN_STATUS && _status_screen) {
            _status_screen->refresh();
        }
    }
}

void UIManager::show_conversation_list() {
    INFO("Showing conversation list");

    _conversation_list_screen->refresh();
    _conversation_list_screen->show();
    _chat_screen->hide();
    _compose_screen->hide();
    _announce_list_screen->hide();
    _status_screen->hide();
    _settings_screen->hide();
    _propagation_nodes_screen->hide();

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
    _announce_list_screen->hide();
    _status_screen->hide();
    _settings_screen->hide();
    _propagation_nodes_screen->hide();

    _current_screen = SCREEN_CHAT;
}

void UIManager::show_compose() {
    INFO("Showing compose screen");

    _compose_screen->clear();
    _compose_screen->show();
    _conversation_list_screen->hide();
    _chat_screen->hide();
    _announce_list_screen->hide();
    _status_screen->hide();
    _settings_screen->hide();
    _propagation_nodes_screen->hide();

    _current_screen = SCREEN_COMPOSE;
}

void UIManager::show_announces() {
    INFO("Showing announces screen");

    _announce_list_screen->refresh();
    _announce_list_screen->show();
    _conversation_list_screen->hide();
    _chat_screen->hide();
    _compose_screen->hide();
    _status_screen->hide();
    _settings_screen->hide();
    _propagation_nodes_screen->hide();

    _current_screen = SCREEN_ANNOUNCES;
}

void UIManager::show_status() {
    INFO("Showing status screen");

    _status_screen->refresh();
    _status_screen->show();
    _conversation_list_screen->hide();
    _chat_screen->hide();
    _compose_screen->hide();
    _announce_list_screen->hide();
    _settings_screen->hide();
    _propagation_nodes_screen->hide();

    _current_screen = SCREEN_STATUS;
}

void UIManager::on_conversation_selected(const Bytes& peer_hash) {
    show_chat(peer_hash);
}

void UIManager::on_new_message() {
    show_compose();
}

void UIManager::show_settings() {
    INFO("Showing settings screen");

    _settings_screen->refresh();
    _settings_screen->show();
    _conversation_list_screen->hide();
    _chat_screen->hide();
    _compose_screen->hide();
    _announce_list_screen->hide();
    _status_screen->hide();
    _propagation_nodes_screen->hide();

    _current_screen = SCREEN_SETTINGS;
}

void UIManager::show_propagation_nodes() {
    INFO("Showing propagation nodes screen");

    if (_propagation_manager) {
        // Load nodes from manager
        // TODO: Get auto-select setting from NVS
        bool auto_select = true;
        Bytes selected_hash = _router.get_outbound_propagation_node();
        _propagation_nodes_screen->load_nodes(*_propagation_manager, selected_hash, auto_select);
    }

    _propagation_nodes_screen->show();
    _conversation_list_screen->hide();
    _chat_screen->hide();
    _compose_screen->hide();
    _announce_list_screen->hide();
    _status_screen->hide();
    _settings_screen->hide();

    _current_screen = SCREEN_PROPAGATION_NODES;
}

void UIManager::set_propagation_node_manager(::LXMF::PropagationNodeManager* manager) {
    _propagation_manager = manager;
}

void UIManager::set_lora_interface(Interface* iface) {
    if (_conversation_list_screen) {
        _conversation_list_screen->set_lora_interface(iface);
    }
}

void UIManager::set_ble_interface(Interface* iface) {
    if (_conversation_list_screen) {
        _conversation_list_screen->set_ble_interface(iface);
    }
}

void UIManager::set_gps(TinyGPSPlus* gps) {
    if (_conversation_list_screen) {
        _conversation_list_screen->set_gps(gps);
    }
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

void UIManager::on_announce_selected(const Bytes& dest_hash) {
    std::string hash_hex = dest_hash.toHex().substr(0, 8);
    std::string msg = "Announce selected: " + hash_hex + "...";
    INFO(msg.c_str());

    // Go directly to chat screen with this destination
    show_chat(dest_hash);
}

void UIManager::on_back_from_announces() {
    show_conversation_list();
}

void UIManager::on_back_from_status() {
    show_conversation_list();
}

void UIManager::on_share_from_status() {
    _status_screen->hide();
    _qr_screen->show();
    _current_screen = SCREEN_QR;
}

void UIManager::on_back_from_qr() {
    _qr_screen->hide();
    _status_screen->show();
    _current_screen = SCREEN_STATUS;
}

void UIManager::on_back_from_settings() {
    show_conversation_list();
}

void UIManager::on_back_from_propagation_nodes() {
    show_settings();
}

void UIManager::on_propagation_node_selected(const Bytes& node_hash) {
    std::string hash_hex = node_hash.toHex().substr(0, 16);
    std::string msg = "Propagation node selected: " + hash_hex + "...";
    INFO(msg.c_str());

    // Set the node in the router
    _router.set_outbound_propagation_node(node_hash);

    // TODO: Save to NVS
}

void UIManager::on_propagation_auto_select_changed(bool enabled) {
    std::string msg = "Propagation auto-select changed: ";
    msg += enabled ? "enabled" : "disabled";
    INFO(msg.c_str());

    if (enabled) {
        // Clear manual selection, router will use best node
        _router.set_outbound_propagation_node(Bytes());
    }

    // TODO: Save to NVS
}

void UIManager::on_propagation_sync() {
    INFO("Requesting messages from propagation node");
    _router.request_messages_from_propagation_node();
}

void UIManager::set_rns_status(bool connected, const String& server_name) {
    if (_status_screen) {
        _status_screen->set_rns_status(connected, server_name);
    }
}

void UIManager::send_message(const Bytes& dest_hash, const String& content) {
    std::string hash_hex = dest_hash.toHex().substr(0, 8);
    std::string msg = "Sending message to " + hash_hex + "...";
    INFO(msg.c_str());

    // Get our source destination (needed for signing)
    Destination source = _router.delivery_destination();

    // Create message content
    Bytes content_bytes((const uint8_t*)content.c_str(), content.length());
    Bytes title;  // Empty title

    // Look up destination identity
    Identity dest_identity = Identity::recall(dest_hash);

    // Create destination object - either real or placeholder
    Destination destination(Type::NONE);
    if (dest_identity) {
        destination = Destination(dest_identity, Type::Destination::OUT, Type::Destination::SINGLE, "lxmf", "delivery");
        INFO("  Destination identity known");
    } else {
        WARNING("  Destination identity not known, message may fail until peer announces");
    }

    // Create message with destination and source objects
    // Source is needed for signing
    ::LXMF::LXMessage message(destination, source, content_bytes, title);

    // If destination identity was unknown, manually set the destination hash
    if (!dest_identity) {
        message.destination_hash(dest_hash);
        DEBUG("  Set destination hash manually");
    }

    // Pack the message to generate hash and signature before saving
    message.pack();

    // Add to UI immediately (optimistic update)
    if (_current_screen == SCREEN_CHAT && _current_peer_hash == dest_hash) {
        _chat_screen->add_message(message, true);
    }

    // Save to store (now has valid hash from pack())
    _store.save_message(message);

    // Queue for sending (pack already called, will use cached packed data)
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
    bool viewing_this_chat = (_current_screen == SCREEN_CHAT && _current_peer_hash == message.source_hash());
    if (viewing_this_chat) {
        _chat_screen->add_message(message, false);
    }

    // Play notification sound if enabled and not viewing this conversation
    if (_settings_screen) {
        const auto& settings = _settings_screen->get_settings();
        if (settings.notification_sound && !viewing_this_chat) {
            Notification::tone_play(1000, 100, settings.notification_volume);  // 1kHz beep, 100ms
        }
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
        case SCREEN_ANNOUNCES:
            _announce_list_screen->refresh();
            break;
        case SCREEN_STATUS:
            _status_screen->refresh();
            break;
        case SCREEN_SETTINGS:
            _settings_screen->refresh();
            break;
        case SCREEN_PROPAGATION_NODES:
            _propagation_nodes_screen->refresh();
            break;
    }
}

} // namespace LXMF
} // namespace UI

#endif // ARDUINO
