#include "MessageStore.h"
#include "../Log.h"
#include "../Utilities/OS.h"

#include <ArduinoJson.h>
#include <algorithm>
#include <sstream>

using namespace LXMF;
using namespace RNS;

// Constructor
MessageStore::MessageStore(const std::string& base_path) :
	_base_path(base_path),
	_initialized(false)
{
	INFO("Initializing MessageStore at: " + _base_path);

	if (initialize_storage()) {
		load_index();
		_initialized = true;
		INFO("MessageStore initialized with " + std::to_string(_conversations.size()) + " conversations");
	} else {
		ERROR("Failed to initialize MessageStore");
	}
}

MessageStore::~MessageStore() {
	if (_initialized) {
		save_index();
	}
	TRACE("MessageStore destroyed");
}

// Initialize storage directories
bool MessageStore::initialize_storage() {
	// Create short directories for SPIFFS compatibility
	// SPIFFS is flat so these are mostly no-ops, but we try anyway
	Utilities::OS::create_directory("/m");  // messages
	Utilities::OS::create_directory("/c");  // conversations

	DEBUG("Storage directories initialized");
	return true;
}

// Load conversation index from disk
void MessageStore::load_index() {
	std::string index_path = "/conv.json";  // Short path for SPIFFS

	if (!Utilities::OS::file_exists(index_path.c_str())) {
		DEBUG("No existing conversation index found");
		return;
	}

	try {
		// Read JSON file via OS abstraction (SPIFFS compatible)
		Bytes data;
		if (Utilities::OS::read_file(index_path.c_str(), data) == 0) {
			WARNING("Failed to read index file or empty: " + index_path);
			return;
		}

		// Parse JSON from bytes
		JsonDocument doc;
		DeserializationError error = deserializeJson(doc, data.data(), data.size());

		if (error) {
			ERROR("Failed to parse conversation index: " + std::string(error.c_str()));
			return;
		}

		// Load conversations
		JsonArray conversations = doc["conversations"].as<JsonArray>();
		for (JsonObject conv : conversations) {
			ConversationInfo info;

			// Parse peer hash
			const char* peer_hex = conv["peer_hash"];
			info.peer_hash.assignHex(peer_hex);

			// Parse message hashes
			JsonArray messages = conv["messages"].as<JsonArray>();
			for (const char* msg_hex : messages) {
				Bytes msg_hash;
				msg_hash.assignHex(msg_hex);
				info.message_hashes.push_back(msg_hash);
			}

			// Parse metadata
			info.last_activity = conv["last_activity"] | 0.0;
			info.unread_count = conv["unread_count"] | 0;

			if (conv.containsKey("last_message_hash")) {
				const char* last_msg_hex = conv["last_message_hash"];
				info.last_message_hash.assignHex(last_msg_hex);
			}

			_conversations[info.peer_hash] = info;
		}

		DEBUG("Loaded " + std::to_string(_conversations.size()) + " conversations from index");

	} catch (const std::exception& e) {
		ERROR("Exception loading conversation index: " + std::string(e.what()));
	}
}

// Save conversation index to disk
bool MessageStore::save_index() {
	std::string index_path = "/conv.json";  // Short path for SPIFFS

	try {
		JsonDocument doc;
		JsonArray conversations = doc["conversations"].to<JsonArray>();

		// Serialize each conversation
		for (const auto& pair : _conversations) {
			const ConversationInfo& info = pair.second;

			JsonObject conv = conversations.add<JsonObject>();
			conv["peer_hash"] = info.peer_hash.toHex();
			conv["last_activity"] = info.last_activity;
			conv["unread_count"] = info.unread_count;

			if (info.last_message_hash) {
				conv["last_message_hash"] = info.last_message_hash.toHex();
			}

			// Serialize message hashes
			JsonArray messages = conv["messages"].to<JsonArray>();
			for (const Bytes& hash : info.message_hashes) {
				messages.add(hash.toHex());
			}
		}

		// Serialize to string then write via OS abstraction (SPIFFS compatible)
		std::string json_str;
		serializeJsonPretty(doc, json_str);
		Bytes data((const uint8_t*)json_str.data(), json_str.size());

		if (Utilities::OS::write_file(index_path.c_str(), data) != data.size()) {
			ERROR("Failed to write index file: " + index_path);
			return false;
		}

		DEBUG("Saved conversation index");
		return true;

	} catch (const std::exception& e) {
		ERROR("Exception saving conversation index: " + std::string(e.what()));
		return false;
	}
}

// Save message to storage
bool MessageStore::save_message(const LXMessage& message) {
	if (!_initialized) {
		ERROR("MessageStore not initialized");
		return false;
	}

	INFO("Saving message: " + message.hash().toHex());

	try {
		// Create JSON document for message
		JsonDocument doc;

		doc["hash"] = message.hash().toHex();
		doc["destination_hash"] = message.destination_hash().toHex();
		doc["source_hash"] = message.source_hash().toHex();
		doc["incoming"] = message.incoming();
		doc["timestamp"] = message.timestamp();
		doc["state"] = static_cast<int>(message.state());

		// Store content as UTF-8 for fast loading (no msgpack unpacking needed)
		std::string content_str((const char*)message.content().data(), message.content().size());
		doc["content"] = content_str;

		// Store the entire packed message to preserve hash/signature
		// This ensures exact reconstruction on load
		doc["packed"] = message.packed().toHex();

		// Write message file via OS abstraction (SPIFFS compatible)
		std::string message_path = get_message_path(message.hash());
		std::string json_str;
		serializeJsonPretty(doc, json_str);
		Bytes data((const uint8_t*)json_str.data(), json_str.size());

		if (Utilities::OS::write_file(message_path.c_str(), data) != data.size()) {
			ERROR("Failed to write message file: " + message_path);
			return false;
		}

		DEBUG("  Message file saved: " + message_path);

		// Update conversation index
		// Determine peer hash (the other party in the conversation)
		// For incoming: peer = source, for outgoing: peer = destination
		Bytes peer_hash = message.incoming() ? message.source_hash() : message.destination_hash();

		// Get or create conversation
		ConversationInfo& conv = _conversations[peer_hash];
		if (!conv.peer_hash) {
			conv.peer_hash = peer_hash;
			DEBUG("  Created new conversation with: " + peer_hash.toHex());
		}

		// Add message to conversation (if not already present)
		bool already_exists = false;
		for (const Bytes& hash : conv.message_hashes) {
			if (hash == message.hash()) {
				already_exists = true;
				break;
			}
		}

		if (!already_exists) {
			conv.message_hashes.push_back(message.hash());
			conv.last_activity = message.timestamp();
			conv.last_message_hash = message.hash();

			// Increment unread count for incoming messages
			if (message.incoming()) {
				conv.unread_count++;
			}

			DEBUG("  Added to conversation (now " + std::to_string(conv.message_hashes.size()) + " messages)");
		}

		// Save updated index
		save_index();

		INFO("Message saved successfully");
		return true;

	} catch (const std::exception& e) {
		ERROR("Exception saving message: " + std::string(e.what()));
		return false;
	}
}

// Load message from storage
LXMessage MessageStore::load_message(const Bytes& message_hash) {
	if (!_initialized) {
		ERROR("MessageStore not initialized");
		return LXMessage(Bytes(), Bytes(), Bytes(), Bytes());
	}

	std::string message_path = get_message_path(message_hash);

	if (!Utilities::OS::file_exists(message_path.c_str())) {
		WARNING("Message file not found: " + message_path);
		return LXMessage(Bytes(), Bytes(), Bytes(), Bytes());
	}

	try {
		// Read JSON file via OS abstraction (SPIFFS compatible)
		Bytes data;
		if (Utilities::OS::read_file(message_path.c_str(), data) == 0) {
			ERROR("Failed to read message file: " + message_path);
			return LXMessage(Bytes(), Bytes(), Bytes(), Bytes());
		}

		JsonDocument doc;
		DeserializationError error = deserializeJson(doc, data.data(), data.size());

		if (error) {
			ERROR("Failed to parse message file: " + std::string(error.c_str()));
			return LXMessage(Bytes(), Bytes(), Bytes(), Bytes());
		}

		// Unpack the message from stored packed bytes
		// This preserves the exact hash and signature
		Bytes packed;
		packed.assignHex(doc["packed"].as<const char*>());

		// Skip signature validation - messages from storage were already validated when received
		LXMessage message = LXMessage::unpack_from_bytes(packed, LXMF::Type::Message::DIRECT, true);

		// Restore incoming flag from storage (unpack_from_bytes defaults to true)
		if (doc.containsKey("incoming")) {
			message.incoming(doc["incoming"].as<bool>());
		}

		DEBUG("Loaded message: " + message_hash.toHex());
		return message;

	} catch (const std::exception& e) {
		ERROR("Exception loading message: " + std::string(e.what()));
		return LXMessage(Bytes(), Bytes(), Bytes(), Bytes());
	}
}

// Load only message metadata (fast path - no msgpack unpacking)
MessageStore::MessageMetadata MessageStore::load_message_metadata(const Bytes& message_hash) {
	MessageMetadata meta;
	meta.valid = false;

	if (!_initialized) {
		return meta;
	}

	std::string message_path = get_message_path(message_hash);

	if (!Utilities::OS::file_exists(message_path.c_str())) {
		return meta;
	}

	try {
		Bytes data;
		if (Utilities::OS::read_file(message_path.c_str(), data) == 0) {
			return meta;
		}

		JsonDocument doc;
		DeserializationError error = deserializeJson(doc, data.data(), data.size());

		if (error) {
			return meta;
		}

		meta.hash = message_hash;

		// Read pre-extracted fields (no msgpack unpacking needed)
		if (doc["content"].is<const char*>()) {
			meta.content = doc["content"].as<std::string>();
		}
		meta.timestamp = doc["timestamp"] | 0.0;
		meta.incoming = doc["incoming"] | true;
		meta.state = doc["state"] | 0;
		meta.valid = true;

		return meta;

	} catch (...) {
		return meta;
	}
}

// Update message state in storage
bool MessageStore::update_message_state(const Bytes& message_hash, Type::Message::State state) {
	if (!_initialized) {
		ERROR("MessageStore not initialized");
		return false;
	}

	std::string message_path = get_message_path(message_hash);

	if (!Utilities::OS::file_exists(message_path.c_str())) {
		WARNING("Message file not found: " + message_path);
		return false;
	}

	try {
		// Read existing JSON
		Bytes data;
		if (Utilities::OS::read_file(message_path.c_str(), data) == 0) {
			ERROR("Failed to read message file: " + message_path);
			return false;
		}

		JsonDocument doc;
		DeserializationError error = deserializeJson(doc, data.data(), data.size());
		if (error) {
			ERROR("Failed to parse message file: " + std::string(error.c_str()));
			return false;
		}

		// Update state
		doc["state"] = static_cast<int>(state);

		// Write back
		std::string json_str;
		serializeJson(doc, json_str);
		if (!Utilities::OS::write_file(message_path.c_str(), Bytes((uint8_t*)json_str.c_str(), json_str.length()))) {
			ERROR("Failed to write message file: " + message_path);
			return false;
		}

		INFO("Message state updated to " + std::to_string(static_cast<int>(state)));
		return true;

	} catch (const std::exception& e) {
		ERROR("Exception updating message state: " + std::string(e.what()));
		return false;
	}
}

// Delete message from storage
bool MessageStore::delete_message(const Bytes& message_hash) {
	if (!_initialized) {
		ERROR("MessageStore not initialized");
		return false;
	}

	INFO("Deleting message: " + message_hash.toHex());

	// Remove message file
	std::string message_path = get_message_path(message_hash);
	if (Utilities::OS::file_exists(message_path.c_str())) {
		if (!Utilities::OS::remove_file(message_path.c_str())) {
			ERROR("Failed to delete message file: " + message_path);
			return false;
		}
	}

	// Update conversation index - remove from all conversations
	for (auto& pair : _conversations) {
		ConversationInfo& conv = pair.second;
		auto it = std::find(conv.message_hashes.begin(), conv.message_hashes.end(), message_hash);
		if (it != conv.message_hashes.end()) {
			conv.message_hashes.erase(it);

			// Update last message if this was it
			if (conv.last_message_hash == message_hash) {
				if (!conv.message_hashes.empty()) {
					conv.last_message_hash = conv.message_hashes.back();
				} else {
					conv.last_message_hash = Bytes();
				}
			}

			DEBUG("  Removed from conversation");
			break;
		}
	}

	save_index();
	INFO("Message deleted");
	return true;
}

// Get list of conversations (sorted by last activity)
std::vector<Bytes> MessageStore::get_conversations() {
	std::vector<std::pair<double, Bytes>> sorted;

	for (const auto& pair : _conversations) {
		sorted.push_back({pair.second.last_activity, pair.first});
	}

	// Sort by last activity (most recent first)
	std::sort(sorted.begin(), sorted.end(),
		[](const std::pair<double, Bytes>& a, const std::pair<double, Bytes>& b) { return a.first > b.first; });

	std::vector<Bytes> result;
	for (const auto& pair : sorted) {
		result.push_back(pair.second);
	}

	return result;
}

// Get conversation info
MessageStore::ConversationInfo MessageStore::get_conversation_info(const Bytes& peer_hash) {
	auto it = _conversations.find(peer_hash);
	if (it != _conversations.end()) {
		return it->second;
	}
	return ConversationInfo();
}

// Get messages for conversation
std::vector<Bytes> MessageStore::get_messages_for_conversation(const Bytes& peer_hash) {
	auto it = _conversations.find(peer_hash);
	if (it != _conversations.end()) {
		return it->second.message_hashes;
	}
	return std::vector<Bytes>();
}

// Mark conversation as read
void MessageStore::mark_conversation_read(const Bytes& peer_hash) {
	auto it = _conversations.find(peer_hash);
	if (it != _conversations.end()) {
		it->second.unread_count = 0;
		save_index();
		DEBUG("Marked conversation as read: " + peer_hash.toHex());
	}
}

// Delete entire conversation
bool MessageStore::delete_conversation(const Bytes& peer_hash) {
	auto it = _conversations.find(peer_hash);
	if (it == _conversations.end()) {
		WARNING("Conversation not found: " + peer_hash.toHex());
		return false;
	}

	INFO("Deleting conversation: " + peer_hash.toHex());

	// Delete all message files
	for (const Bytes& message_hash : it->second.message_hashes) {
		std::string message_path = get_message_path(message_hash);
		if (Utilities::OS::file_exists(message_path.c_str())) {
			Utilities::OS::remove_file(message_path.c_str());
		}
	}

	// Remove from index
	_conversations.erase(it);
	save_index();

	INFO("Conversation deleted");
	return true;
}

// Get total message count
size_t MessageStore::get_message_count() const {
	size_t count = 0;
	for (const auto& pair : _conversations) {
		count += pair.second.message_hashes.size();
	}
	return count;
}

// Get conversation count
size_t MessageStore::get_conversation_count() const {
	return _conversations.size();
}

// Get total unread count
size_t MessageStore::get_unread_count() const {
	size_t count = 0;
	for (const auto& pair : _conversations) {
		count += pair.second.unread_count;
	}
	return count;
}

// Clear all data
bool MessageStore::clear_all() {
	INFO("Clearing all message store data");

	// Delete all message files
	for (const auto& pair : _conversations) {
		for (const Bytes& message_hash : pair.second.message_hashes) {
			std::string message_path = get_message_path(message_hash);
			if (Utilities::OS::file_exists(message_path.c_str())) {
				Utilities::OS::remove_file(message_path.c_str());
			}
		}
	}

	// Clear in-memory index
	_conversations.clear();

	// Save empty index
	save_index();

	INFO("Message store cleared");
	return true;
}

// Get message file path
// Use short path for SPIFFS compatibility (32 char filename limit)
// Format: /m/<first12chars>.j (12 chars of hash = 6 bytes = plenty unique for local store)
std::string MessageStore::get_message_path(const Bytes& message_hash) const {
	return "/m/" + message_hash.toHex().substr(0, 12) + ".j";
}

// Get conversation directory path
std::string MessageStore::get_conversation_path(const Bytes& peer_hash) const {
	return "/c/" + peer_hash.toHex().substr(0, 12);
}

// Determine peer hash from message
Bytes MessageStore::get_peer_hash(const LXMessage& message, const Bytes& our_hash) const {
	// For incoming messages: peer = source
	// For outgoing messages: peer = destination
	if (message.incoming()) {
		return message.source_hash();
	} else {
		return message.destination_hash();
	}
}
