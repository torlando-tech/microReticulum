#include "MessageStore.h"
#include "../Log.h"
#include "../Utilities/OS.h"

#include <ArduinoJson.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <utility>

#ifdef ARDUINO
#include <esp_heap_caps.h>
#endif

using namespace LXMF;
using namespace RNS;

namespace {

static std::string make_message_preview(const Bytes& content) {
	size_t preview_len = std::min<size_t>(content.size(), 60);
	std::string preview;
	preview.assign(reinterpret_cast<const char*>(content.data()), preview_len);
	if (content.size() > preview_len) {
		preview += "...";
	}
	return preview;
}

static bool write_json_document(const std::string& path, JsonDocument& doc) {
	std::string json_str;
	serializeJson(doc, json_str);
	Bytes data(reinterpret_cast<const uint8_t*>(json_str.data()), json_str.size());
	return Utilities::OS::write_file(path.c_str(), data) == data.size();
}

static bool write_bytes_file(const std::string& path, const Bytes& data) {
	return Utilities::OS::write_file(path.c_str(), data) == data.size();
}

}  // namespace

// ConversationInfo helper methods
bool MessageStore::ConversationInfo::add_message_hash(const Bytes& hash) {
	// Check if already exists
	if (has_message(hash)) {
		return false;
	}
	// Check if pool is full
	if (message_count >= MAX_MESSAGES_PER_CONVERSATION) {
		return false;
	}
	// Copy hash to fixed array
	size_t len = std::min(hash.size(), MESSAGE_HASH_SIZE);
	memcpy(message_hashes[message_count], hash.data(), len);
	if (len < MESSAGE_HASH_SIZE) {
		memset(message_hashes[message_count] + len, 0, MESSAGE_HASH_SIZE - len);
	}
	++message_count;
	return true;
}

bool MessageStore::ConversationInfo::has_message(const Bytes& hash) const {
	if (hash.size() == 0 || hash.size() > MESSAGE_HASH_SIZE) return false;
	for (size_t i = 0; i < message_count; ++i) {
		if (memcmp(message_hashes[i], hash.data(), hash.size()) == 0) {
			return true;
		}
	}
	return false;
}

bool MessageStore::ConversationInfo::remove_message_hash(const Bytes& hash) {
	if (hash.size() == 0 || hash.size() > MESSAGE_HASH_SIZE) return false;
	for (size_t i = 0; i < message_count; ++i) {
		if (memcmp(message_hashes[i], hash.data(), hash.size()) == 0) {
			// Shift remaining elements down
			for (size_t j = i; j < message_count - 1; ++j) {
				memcpy(message_hashes[j], message_hashes[j + 1], MESSAGE_HASH_SIZE);
			}
			memset(message_hashes[message_count - 1], 0, MESSAGE_HASH_SIZE);
			--message_count;
			return true;
		}
	}
	return false;
}

void MessageStore::ConversationInfo::clear() {
	memset(peer_hash, 0, PEER_HASH_SIZE);
	memset(message_hashes, 0, sizeof(message_hashes));
	message_count = 0;
	last_activity = 0.0;
	unread_count = 0;
	memset(last_message_hash, 0, MESSAGE_HASH_SIZE);
	memset(last_message_preview, 0, sizeof(last_message_preview));
}

// ConversationSlot helper method
void MessageStore::ConversationSlot::clear() {
	in_use = false;
	memset(peer_hash, 0, PEER_HASH_SIZE);
	info.clear();
}

// Constructor
MessageStore::MessageStore(const std::string& base_path) :
	_base_path(base_path),
	_initialized(false)
{
	INFO("Initializing MessageStore at: " + _base_path);

	// Allocate conversation pool off internal heap when PSRAM is available.
	const size_t pool_bytes = sizeof(ConversationSlot) * MAX_CONVERSATIONS;
	void* pool_mem = nullptr;
	bool pool_from_psram = false;
#ifdef ARDUINO
	pool_mem = heap_caps_malloc(pool_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
	if (!pool_mem) {
		WARNING("MessageStore: PSRAM pool allocation failed, falling back to internal heap");
		pool_mem = std::malloc(pool_bytes);
	} else {
		pool_from_psram = true;
	}
#else
	pool_mem = std::malloc(pool_bytes);
#endif

	if (!pool_mem) {
		ERROR("Failed to allocate conversation pool");
		return;
	}

	_conversations_pool = std::unique_ptr<ConversationSlot[], ConversationPoolDeleter>(
		static_cast<ConversationSlot*>(pool_mem),
		ConversationPoolDeleter(pool_from_psram)
	);
	for (size_t i = 0; i < MAX_CONVERSATIONS; ++i) {
		_conversations_pool[i].clear();
	}

	if (initialize_storage()) {
		_initialized = true;
		load_index();
		INFO("MessageStore initialized with " + std::to_string(count_conversations()) + " conversations");
	} else {
		ERROR("Failed to initialize MessageStore");
	}
}

MessageStore::~MessageStore() {
	if (_initialized) {
		save_index();
	}
	_conversations_pool.reset();
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
	bool index_needs_save = false;
	std::vector<std::pair<size_t, Bytes>> preview_backfill;

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

		// Parse JSON from bytes using reusable document to reduce heap fragmentation
		_json_doc.clear();
		DeserializationError error = deserializeJson(_json_doc, data.data(), data.size());

		if (error) {
			ERROR("Failed to parse conversation index: " + std::string(error.c_str()));
			return;
		}

		// Load conversations into pool
		JsonArray conversations = _json_doc["conversations"].as<JsonArray>();
		size_t slot_index = 0;
		for (JsonObject conv : conversations) {
			if (slot_index >= MAX_CONVERSATIONS) {
				WARNING("Too many conversations in index, some will be skipped");
				break;
			}

			ConversationSlot& slot = _conversations_pool[slot_index];
			slot.in_use = true;

			// Parse peer hash
			const char* peer_hex = conv["peer_hash"];
			Bytes peer_bytes;
			peer_bytes.assignHex(peer_hex);
			slot.set_peer_hash(peer_bytes);
			slot.info.set_peer_hash(peer_bytes);

			// Parse message hashes
			JsonArray messages = conv["messages"].as<JsonArray>();
			for (const char* msg_hex : messages) {
				if (slot.info.message_count >= MAX_MESSAGES_PER_CONVERSATION) {
					WARNING("Too many messages in conversation, some will be skipped");
					break;
				}
				Bytes msg_hash;
				msg_hash.assignHex(msg_hex);
				slot.info.add_message_hash(msg_hash);
			}

			// Parse metadata
			slot.info.last_activity = conv["last_activity"] | 0.0;
			slot.info.unread_count = conv["unread_count"] | 0;

			if (!conv["last_message_hash"].isNull()) {
				const char* last_msg_hex = conv["last_message_hash"];
				Bytes last_msg_bytes;
				last_msg_bytes.assignHex(last_msg_hex);
				slot.info.set_last_message_hash(last_msg_bytes);
			}

			if (conv["last_message_preview"].is<const char*>()) {
				slot.info.set_last_message_preview(conv["last_message_preview"].as<std::string>());
			} else {
				Bytes last_msg_hash = slot.info.last_message_hash_bytes();
				if (last_msg_hash) {
					preview_backfill.push_back(std::make_pair(slot_index, last_msg_hash));
				}
			}

			++slot_index;
		}

		for (const auto& entry : preview_backfill) {
			if (entry.first >= MAX_CONVERSATIONS) {
				continue;
			}

			ConversationSlot& slot = _conversations_pool[entry.first];
			if (!slot.in_use) {
				continue;
			}

			MessageMetadata meta = load_message_metadata(entry.second);
			if (meta.valid && !meta.content.empty()) {
				slot.info.set_last_message_preview(make_message_preview(Bytes(
					reinterpret_cast<const uint8_t*>(meta.content.data()),
					meta.content.size()
				)));
				index_needs_save = true;
			}
		}

		if (index_needs_save) {
			save_index();
		}

		DEBUG("Loaded " + std::to_string(count_conversations()) + " conversations from index");

	} catch (const std::exception& e) {
		ERROR("Exception loading conversation index: " + std::string(e.what()));
	}
}

// Save conversation index to disk
bool MessageStore::save_index() {
	std::string index_path = "/conv.json";  // Short path for SPIFFS

	try {
		// Use reusable document to reduce heap fragmentation
		_json_doc.clear();
		JsonArray conversations = _json_doc["conversations"].to<JsonArray>();

		// Serialize each active conversation from pool
		for (size_t i = 0; i < MAX_CONVERSATIONS; ++i) {
			const ConversationSlot& slot = _conversations_pool[i];
			if (!slot.in_use) {
				continue;
			}

			const ConversationInfo& info = slot.info;

			JsonObject conv = conversations.add<JsonObject>();
			conv["peer_hash"] = slot.peer_hash_bytes().toHex();
			conv["last_activity"] = info.last_activity;
			conv["unread_count"] = info.unread_count;

			Bytes last_msg = info.last_message_hash_bytes();
			if (last_msg) {
				conv["last_message_hash"] = last_msg.toHex();
			}

			if (info.last_message_preview_cstr()[0] != '\0') {
				conv["last_message_preview"] = info.last_message_preview_cstr();
			}

			// Serialize message hashes
			JsonArray messages = conv["messages"].to<JsonArray>();
			for (size_t j = 0; j < info.message_count; ++j) {
				messages.add(info.message_hash_bytes(j).toHex());
			}
		}

		// Serialize to string then write via OS abstraction (SPIFFS compatible)
		std::string json_str;
		serializeJson(_json_doc, json_str);
		Bytes data(reinterpret_cast<const uint8_t*>(json_str.data()), json_str.size());

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
		const Bytes message_hash = message.hash();
		const std::string metadata_path = get_message_metadata_path(message_hash);
		const std::string payload_path = get_message_payload_path(message_hash);
		const std::string legacy_path = get_legacy_message_path(message_hash);
		const std::string preview = make_message_preview(message.content());

		// Build compact metadata document only. Packed payload lives in a separate file.
		_json_doc.clear();
		_json_doc["hash"] = message_hash.toHex();
		_json_doc["destination_hash"] = message.destination_hash().toHex();
		_json_doc["source_hash"] = message.source_hash().toHex();
		_json_doc["incoming"] = message.incoming();
		_json_doc["timestamp"] = message.timestamp();
		_json_doc["state"] = static_cast<int>(message.state());
		_json_doc["propagated"] = false;
		_json_doc["content"] = std::string(reinterpret_cast<const char*>(message.content().data()), message.content().size());

		if (!write_json_document(metadata_path, _json_doc)) {
			ERROR("Failed to write message metadata: " + metadata_path);
			return false;
		}

		const Bytes& packed = message.packed();
		if (!write_bytes_file(payload_path, packed)) {
			ERROR("Failed to write message payload: " + payload_path);
			Utilities::OS::remove_file(metadata_path.c_str());
			return false;
		}

		// Remove any legacy monolithic file so the split layout is canonical.
		if (Utilities::OS::file_exists(legacy_path.c_str())) {
			Utilities::OS::remove_file(legacy_path.c_str());
		}

		DEBUG("  Message metadata saved: " + metadata_path);
		DEBUG("  Message payload saved: " + payload_path);

		// Update conversation index
		// Determine peer hash (the other party in the conversation)
		// For incoming: peer = source, for outgoing: peer = destination
		Bytes peer_hash = message.incoming() ? message.source_hash() : message.destination_hash();

		// Get or create conversation slot
		ConversationSlot* slot = get_or_create_conversation(peer_hash);
		if (!slot) {
			ERROR("Conversation pool is full, cannot add message");
			return false;
		}

		ConversationInfo& conv = slot->info;

		// Add message to conversation (if not already present)
		bool already_exists = conv.has_message(message.hash());

		if (!already_exists) {
			if (!conv.add_message_hash(message.hash())) {
				WARNING("Message pool full for conversation: " + peer_hash.toHex());
			} else {
				conv.last_activity = message.timestamp();
				conv.set_last_message_hash(message_hash);
				conv.set_last_message_preview(preview);

				// Increment unread count for incoming messages
				if (message.incoming()) {
					conv.unread_count++;
				}

				DEBUG("  Added to conversation (now " + std::to_string(conv.message_count) + " messages)");
			}
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

	const std::string metadata_path = get_message_metadata_path(message_hash);
	const std::string payload_path = get_message_payload_path(message_hash);
	const std::string legacy_path = get_legacy_message_path(message_hash);

	try {
		Bytes packed;
		bool incoming = true;

		if (Utilities::OS::file_exists(payload_path.c_str())) {
			if (Utilities::OS::read_file(payload_path.c_str(), packed) == 0) {
				ERROR("Failed to read message payload: " + payload_path);
				return LXMessage(Bytes(), Bytes(), Bytes(), Bytes());
			}

			if (Utilities::OS::file_exists(metadata_path.c_str())) {
				Bytes metadata_bytes;
				if (Utilities::OS::read_file(metadata_path.c_str(), metadata_bytes) > 0) {
					_json_doc.clear();
					DeserializationError error = deserializeJson(_json_doc, metadata_bytes.data(), metadata_bytes.size());
					if (!error && !_json_doc["incoming"].isNull()) {
						incoming = _json_doc["incoming"].as<bool>();
					}
				}
			}
		} else {
			std::string source_path = legacy_path;
			if (!Utilities::OS::file_exists(source_path.c_str())) {
				source_path = metadata_path;
			}
			if (!Utilities::OS::file_exists(source_path.c_str())) {
				WARNING("Message file not found: " + source_path);
				return LXMessage(Bytes(), Bytes(), Bytes(), Bytes());
			}

			Bytes data;
			if (Utilities::OS::read_file(source_path.c_str(), data) == 0) {
				ERROR("Failed to read message file: " + source_path);
				return LXMessage(Bytes(), Bytes(), Bytes(), Bytes());
			}

			_json_doc.clear();
			DeserializationError error = deserializeJson(_json_doc, data.data(), data.size());
			if (error) {
				ERROR("Failed to parse message file: " + std::string(error.c_str()));
				return LXMessage(Bytes(), Bytes(), Bytes(), Bytes());
			}

			if (!_json_doc["incoming"].isNull()) {
				incoming = _json_doc["incoming"].as<bool>();
			}

			const char* packed_hex = _json_doc["packed"].as<const char*>();
			if (!packed_hex || packed_hex[0] == '\0') {
				ERROR("Message file missing packed payload: " + source_path);
				return LXMessage(Bytes(), Bytes(), Bytes(), Bytes());
			}

			packed.assignHex(packed_hex);

			if (!Utilities::OS::file_exists(metadata_path.c_str())) {
				JsonDocument metadata_doc;
				metadata_doc["hash"] = _json_doc["hash"] | message_hash.toHex();
				metadata_doc["destination_hash"] = _json_doc["destination_hash"] | "";
				metadata_doc["source_hash"] = _json_doc["source_hash"] | "";
				metadata_doc["incoming"] = incoming;
				metadata_doc["timestamp"] = _json_doc["timestamp"] | 0.0;
				metadata_doc["state"] = _json_doc["state"] | 0;
				metadata_doc["propagated"] = _json_doc["propagated"] | false;
				if (_json_doc["content"].is<const char*>()) {
					metadata_doc["content"] = _json_doc["content"].as<std::string>();
				}
				if (write_json_document(metadata_path, metadata_doc)) {
					write_bytes_file(payload_path, packed);
					Utilities::OS::remove_file(legacy_path.c_str());
				}
			}
		}

		LXMessage message = LXMessage::unpack_from_bytes(packed, LXMF::Type::Message::DIRECT, true);
		message.incoming(incoming);
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

	const std::string metadata_path = get_message_metadata_path(message_hash);
	const std::string legacy_path = get_legacy_message_path(message_hash);

	try {
		std::string source_path;
		if (Utilities::OS::file_exists(metadata_path.c_str())) {
			source_path = metadata_path;
		} else if (Utilities::OS::file_exists(legacy_path.c_str())) {
			source_path = legacy_path;
		} else {
			return meta;
		}

		Bytes data;
		if (Utilities::OS::read_file(source_path.c_str(), data) == 0) {
			return meta;
		}

		_json_doc.clear();
		DeserializationError error = deserializeJson(_json_doc, data.data(), data.size());
		if (error) {
			return meta;
		}

		meta.hash = message_hash;
		if (_json_doc["content"].is<const char*>()) {
			meta.content = _json_doc["content"].as<std::string>();
		}
		meta.timestamp = _json_doc["timestamp"] | 0.0;
		meta.incoming = _json_doc["incoming"] | true;
		meta.state = _json_doc["state"] | 0;
		meta.propagated = _json_doc["propagated"] | false;
		meta.valid = true;

		return meta;

	} catch (...) {
		return meta;
	}
}

bool MessageStore::update_message_metadata(
	const Bytes& message_hash,
	const Type::Message::State* state,
	const bool* propagated
) {
	if (!_initialized) {
		ERROR("MessageStore not initialized");
		return false;
	}

	const std::string metadata_path = get_message_metadata_path(message_hash);
	const std::string payload_path = get_message_payload_path(message_hash);
	const std::string legacy_path = get_legacy_message_path(message_hash);

	const bool has_metadata = Utilities::OS::file_exists(metadata_path.c_str());
	const bool has_legacy = Utilities::OS::file_exists(legacy_path.c_str());
	if (!has_metadata && !has_legacy) {
		WARNING("Message file not found: " + metadata_path);
		return false;
	}

	try {
		const std::string source_path = has_metadata ? metadata_path : legacy_path;
		Bytes data;
		if (Utilities::OS::read_file(source_path.c_str(), data) == 0) {
			ERROR("Failed to read message file: " + source_path);
			return false;
		}

		_json_doc.clear();
		DeserializationError error = deserializeJson(_json_doc, data.data(), data.size());
		if (error) {
			ERROR("Failed to parse message file: " + std::string(error.c_str()));
			return false;
		}

		bool update_migrated_layout = !has_metadata && has_legacy;
		if (state) {
			_json_doc["state"] = static_cast<int>(*state);
		}

		if (propagated) {
			_json_doc["propagated"] = *propagated;
		}

		if (update_migrated_layout) {
			const char* packed_hex = _json_doc["packed"].as<const char*>();
			if (packed_hex && packed_hex[0] != '\0') {
				Bytes packed;
				packed.assignHex(packed_hex);

				JsonDocument metadata_doc;
				metadata_doc["hash"] = _json_doc["hash"] | message_hash.toHex();
				metadata_doc["destination_hash"] = _json_doc["destination_hash"] | "";
				metadata_doc["source_hash"] = _json_doc["source_hash"] | "";
				metadata_doc["incoming"] = _json_doc["incoming"] | true;
				metadata_doc["timestamp"] = _json_doc["timestamp"] | 0.0;
				metadata_doc["state"] = _json_doc["state"] | 0;
				metadata_doc["propagated"] = _json_doc["propagated"] | false;
				if (_json_doc["content"].is<const char*>()) {
					metadata_doc["content"] = _json_doc["content"].as<std::string>();
				}

				if (!write_json_document(metadata_path, metadata_doc)) {
					ERROR("Failed to write message metadata: " + metadata_path);
					return false;
				}

				if (!write_bytes_file(payload_path, packed)) {
					ERROR("Failed to write message payload: " + payload_path);
					return false;
				}

				Utilities::OS::remove_file(legacy_path.c_str());
			} else {
				ERROR("Legacy message missing packed payload: " + legacy_path);
				return false;
			}
		} else if (!write_json_document(metadata_path, _json_doc)) {
			ERROR("Failed to write message metadata: " + metadata_path);
			return false;
		}

		if (state && propagated) {
			INFO("Message delivery metadata updated: state=" +
				std::to_string(static_cast<int>(*state)) +
				", propagated=" + std::string(*propagated ? "true" : "false"));
		} else if (state) {
			INFO("Message state updated to " + std::to_string(static_cast<int>(*state)));
		} else if (propagated) {
			INFO("Message propagated flag updated to " + std::string(*propagated ? "true" : "false"));
		}
		return true;

	} catch (const std::exception& e) {
		ERROR("Exception updating message metadata: " + std::string(e.what()));
		return false;
	}
}

// Update message state in storage
bool MessageStore::update_message_state(const Bytes& message_hash, Type::Message::State state) {
	return update_message_metadata(message_hash, &state, nullptr);
}

bool MessageStore::update_message_delivery_status(
	const Bytes& message_hash,
	Type::Message::State state,
	bool propagated
) {
	return update_message_metadata(message_hash, &state, &propagated);
}

// Delete message from storage
bool MessageStore::delete_message(const Bytes& message_hash) {
	if (!_initialized) {
		ERROR("MessageStore not initialized");
		return false;
	}

	INFO("Deleting message: " + message_hash.toHex());

	// Remove message files (split metadata/payload plus legacy monolith)
	const std::string metadata_path = get_message_metadata_path(message_hash);
	const std::string payload_path = get_message_payload_path(message_hash);
	const std::string legacy_path = get_legacy_message_path(message_hash);

	if (Utilities::OS::file_exists(metadata_path.c_str()) &&
	    !Utilities::OS::remove_file(metadata_path.c_str())) {
		ERROR("Failed to delete message metadata: " + metadata_path);
		return false;
	}
	if (Utilities::OS::file_exists(payload_path.c_str()) &&
	    !Utilities::OS::remove_file(payload_path.c_str())) {
		ERROR("Failed to delete message payload: " + payload_path);
		return false;
	}
	if (Utilities::OS::file_exists(legacy_path.c_str())) {
		Utilities::OS::remove_file(legacy_path.c_str());
	}

	// Update conversation index - remove from all conversations
	for (size_t i = 0; i < MAX_CONVERSATIONS; ++i) {
		ConversationSlot& slot = _conversations_pool[i];
		if (!slot.in_use) {
			continue;
		}

		ConversationInfo& conv = slot.info;
		if (conv.remove_message_hash(message_hash)) {
			// Update last message if this was it
			if (conv.last_message_hash_bytes() == message_hash) {
				if (conv.message_count > 0) {
					conv.set_last_message_hash(conv.message_hash_bytes(conv.message_count - 1));
				} else {
					memset(conv.last_message_hash, 0, MESSAGE_HASH_SIZE);
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

	for (size_t i = 0; i < MAX_CONVERSATIONS; ++i) {
		const ConversationSlot& slot = _conversations_pool[i];
		if (slot.in_use) {
			sorted.push_back({slot.info.last_activity, slot.peer_hash_bytes()});
		}
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
const MessageStore::ConversationInfo* MessageStore::get_conversation_info(const Bytes& peer_hash) const {
	const ConversationSlot* slot = find_conversation(peer_hash);
	if (slot) {
		return &slot->info;
	}
	return nullptr;
}

std::vector<MessageStore::ConversationSummary> MessageStore::get_conversation_summaries() const {
	std::vector<ConversationSummary> summaries;
	summaries.reserve(count_conversations());

	for (size_t i = 0; i < MAX_CONVERSATIONS; ++i) {
		const ConversationSlot& slot = _conversations_pool[i];
		if (!slot.in_use) {
			continue;
		}

		ConversationSummary summary;
		summary.peer_hash = slot.peer_hash_bytes();
		summary.last_activity = slot.info.last_activity;
		summary.unread_count = static_cast<uint16_t>(slot.info.unread_count);
		summary.last_message_preview = slot.info.last_message_preview_cstr();
		summaries.push_back(summary);
	}

	std::sort(
		summaries.begin(),
		summaries.end(),
		[](const ConversationSummary& a, const ConversationSummary& b) {
			return a.last_activity > b.last_activity;
		}
	);

	return summaries;
}

// Get messages for conversation
std::vector<Bytes> MessageStore::get_messages_for_conversation(const Bytes& peer_hash) {
	const ConversationSlot* slot = find_conversation(peer_hash);
	if (slot) {
		std::vector<Bytes> result;
		result.reserve(slot->info.message_count);
		for (size_t i = 0; i < slot->info.message_count; ++i) {
			result.push_back(slot->info.message_hash_bytes(i));
		}
		return result;
	}
	return std::vector<Bytes>();
}

// Mark conversation as read
void MessageStore::mark_conversation_read(const Bytes& peer_hash) {
	ConversationSlot* slot = find_conversation(peer_hash);
	if (slot) {
		slot->info.unread_count = 0;
		save_index();
		DEBUG("Marked conversation as read: " + peer_hash.toHex());
	}
}

// Delete entire conversation
bool MessageStore::delete_conversation(const Bytes& peer_hash) {
	ConversationSlot* slot = find_conversation(peer_hash);
	if (!slot) {
		WARNING("Conversation not found: " + peer_hash.toHex());
		return false;
	}

	INFO("Deleting conversation: " + peer_hash.toHex());

	// Delete all message files
	for (size_t i = 0; i < slot->info.message_count; ++i) {
		const Bytes hash = slot->info.message_hash_bytes(i);
		const std::string metadata_path = get_message_metadata_path(hash);
		const std::string payload_path = get_message_payload_path(hash);
		const std::string legacy_path = get_legacy_message_path(hash);
		if (Utilities::OS::file_exists(metadata_path.c_str())) {
			Utilities::OS::remove_file(metadata_path.c_str());
		}
		if (Utilities::OS::file_exists(payload_path.c_str())) {
			Utilities::OS::remove_file(payload_path.c_str());
		}
		if (Utilities::OS::file_exists(legacy_path.c_str())) {
			Utilities::OS::remove_file(legacy_path.c_str());
		}
	}

	// Clear slot and mark as not in use
	slot->clear();
	save_index();

	INFO("Conversation deleted");
	return true;
}

// Get total message count
size_t MessageStore::get_message_count() const {
	size_t count = 0;
	for (size_t i = 0; i < MAX_CONVERSATIONS; ++i) {
		if (_conversations_pool[i].in_use) {
			count += _conversations_pool[i].info.message_count;
		}
	}
	return count;
}

// Get conversation count
size_t MessageStore::get_conversation_count() const {
	return count_conversations();
}

// Get total unread count
size_t MessageStore::get_unread_count() const {
	size_t count = 0;
	for (size_t i = 0; i < MAX_CONVERSATIONS; ++i) {
		if (_conversations_pool[i].in_use) {
			count += _conversations_pool[i].info.unread_count;
		}
	}
	return count;
}

// Clear all data
bool MessageStore::clear_all() {
	INFO("Clearing all message store data");

	// Delete all message files
	for (size_t i = 0; i < MAX_CONVERSATIONS; ++i) {
		ConversationSlot& slot = _conversations_pool[i];
		if (!slot.in_use) {
			continue;
		}
		for (size_t j = 0; j < slot.info.message_count; ++j) {
			const Bytes hash = slot.info.message_hash_bytes(j);
			const std::string metadata_path = get_message_metadata_path(hash);
			const std::string payload_path = get_message_payload_path(hash);
			const std::string legacy_path = get_legacy_message_path(hash);
			if (Utilities::OS::file_exists(metadata_path.c_str())) {
				Utilities::OS::remove_file(metadata_path.c_str());
			}
			if (Utilities::OS::file_exists(payload_path.c_str())) {
				Utilities::OS::remove_file(payload_path.c_str());
			}
			if (Utilities::OS::file_exists(legacy_path.c_str())) {
				Utilities::OS::remove_file(legacy_path.c_str());
			}
		}
		slot.clear();
	}

	// Save empty index
	save_index();

	INFO("Message store cleared");
	return true;
}

// Get message file path
// Use short path for SPIFFS compatibility (32 char filename limit)
// Format: /m/<first12chars>.j (12 chars of hash = 6 bytes = plenty unique for local store)
std::string MessageStore::get_message_path(const Bytes& message_hash) const {
	return get_legacy_message_path(message_hash);
}

std::string MessageStore::get_message_metadata_path(const Bytes& message_hash) const {
	return "/m/" + message_hash.toHex().substr(0, 12) + ".m";
}

std::string MessageStore::get_message_payload_path(const Bytes& message_hash) const {
	return "/m/" + message_hash.toHex().substr(0, 12) + ".p";
}

std::string MessageStore::get_legacy_message_path(const Bytes& message_hash) const {
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

// Find a conversation slot by peer hash
MessageStore::ConversationSlot* MessageStore::find_conversation(const Bytes& peer_hash) {
	for (size_t i = 0; i < MAX_CONVERSATIONS; ++i) {
		if (_conversations_pool[i].in_use && _conversations_pool[i].peer_hash_equals(peer_hash)) {
			return &_conversations_pool[i];
		}
	}
	return nullptr;
}

const MessageStore::ConversationSlot* MessageStore::find_conversation(const Bytes& peer_hash) const {
	for (size_t i = 0; i < MAX_CONVERSATIONS; ++i) {
		if (_conversations_pool[i].in_use && _conversations_pool[i].peer_hash_equals(peer_hash)) {
			return &_conversations_pool[i];
		}
	}
	return nullptr;
}

// Get or create a conversation slot for a peer
MessageStore::ConversationSlot* MessageStore::get_or_create_conversation(const Bytes& peer_hash) {
	// First try to find existing
	ConversationSlot* slot = find_conversation(peer_hash);
	if (slot) {
		return slot;
	}

	// Find a free slot
	for (size_t i = 0; i < MAX_CONVERSATIONS; ++i) {
		if (!_conversations_pool[i].in_use) {
			_conversations_pool[i].in_use = true;
			_conversations_pool[i].set_peer_hash(peer_hash);
			_conversations_pool[i].info.set_peer_hash(peer_hash);
			DEBUG("  Created new conversation with: " + peer_hash.toHex());
			return &_conversations_pool[i];
		}
	}

	return nullptr;  // Pool is full
}

// Count number of active conversations in pool
size_t MessageStore::count_conversations() const {
	size_t count = 0;
	for (size_t i = 0; i < MAX_CONVERSATIONS; ++i) {
		if (_conversations_pool[i].in_use) {
			++count;
		}
	}
	return count;
}
