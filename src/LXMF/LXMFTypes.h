#pragma once

#include "../Bytes.h"
#include "../Type.h"

#include <cstdint>
#include <functional>
#include <map>

namespace RNS { namespace LXMF {

//=============================================================================
// App Names (must match Python for announce filtering)
//=============================================================================
static constexpr const char* APP_NAME = "lxmf";
static constexpr const char* ASPECT_DELIVERY = "delivery";
static constexpr const char* ASPECT_PROPAGATION = "propagation";

//=============================================================================
// Wire Format Constants (must match Python LXMF exactly)
// Reference: LXMF/LXMessage.py lines 54-62
//=============================================================================
namespace Wire {
	// Hash and signature lengths from RNS Identity
	static constexpr size_t DESTINATION_LENGTH = Type::Identity::TRUNCATED_HASHLENGTH / 8;  // 16 bytes
	static constexpr size_t SIGNATURE_LENGTH = Type::Identity::SIGLENGTH / 8;               // 64 bytes

	// Msgpack overhead for timestamp and structure
	static constexpr size_t TIMESTAMP_SIZE = 9;    // msgpack float64: 1 (marker) + 8 (data)
	static constexpr size_t STRUCT_OVERHEAD = 6;   // fixarray(4) + 2*bin8(0) + fixmap(0) = 1 + 2 + 2 + 1

	// Total LXMF overhead per message: 111 bytes minimum
	// 16 (dest) + 16 (src) + 64 (sig) + 9 (timestamp) + 6 (struct)
	static constexpr size_t LXMF_OVERHEAD = (2 * DESTINATION_LENGTH) + SIGNATURE_LENGTH +
	                                         TIMESTAMP_SIZE + STRUCT_OVERHEAD;

	// Maximum content sizes for different delivery methods
	// With MTU=500, ENCRYPTED_MDU is ~391 bytes
	static constexpr size_t ENCRYPTED_PACKET_MDU = Type::Packet::ENCRYPTED_MDU + TIMESTAMP_SIZE;

	// Single-packet LXMF max content: ~295 bytes (infer dest from packet)
	static constexpr size_t ENCRYPTED_PACKET_MAX_CONTENT = ENCRYPTED_PACKET_MDU - LXMF_OVERHEAD + DESTINATION_LENGTH;

	// Link packet MDU is 431 bytes
	static constexpr size_t LINK_PACKET_MDU = Type::Link::MDU;

	// Single-packet over link max content: ~319 bytes
	static constexpr size_t LINK_PACKET_MAX_CONTENT = LINK_PACKET_MDU - LXMF_OVERHEAD;

	// Plain packet max content: ~368 bytes
	static constexpr size_t PLAIN_PACKET_MDU = Type::Packet::PLAIN_MDU;
	static constexpr size_t PLAIN_PACKET_MAX_CONTENT = PLAIN_PACKET_MDU - LXMF_OVERHEAD + DESTINATION_LENGTH;
}

//=============================================================================
// Message States
// Reference: LXMF/LXMessage.py lines 14-22
//=============================================================================
enum class MessageState : uint8_t {
	GENERATING = 0x00,
	OUTBOUND   = 0x01,
	SENDING    = 0x02,
	SENT       = 0x04,
	DELIVERED  = 0x08,
	REJECTED   = 0xFD,
	CANCELLED  = 0xFE,
	FAILED     = 0xFF
};

//=============================================================================
// Message Representation (how message is transmitted)
// Reference: LXMF/LXMessage.py lines 24-27
//=============================================================================
enum class Representation : uint8_t {
	UNKNOWN  = 0x00,
	PACKET   = 0x01,  // Single packet (small messages)
	RESOURCE = 0x02   // RNS Resource transfer (large messages)
};

//=============================================================================
// Delivery Methods
// Reference: LXMF/LXMessage.py lines 29-33
//=============================================================================
enum class DeliveryMethod : uint8_t {
	UNKNOWN       = 0x00,
	OPPORTUNISTIC = 0x01,  // Single encrypted packet, no link
	DIRECT        = 0x02,  // Via established RNS Link
	PROPAGATED    = 0x03   // Via propagation node
	// PAPER = 0x05 - Not implemented for embedded
};

//=============================================================================
// Signature Validation Status
// Reference: LXMF/LXMessage.py lines 35-37
//=============================================================================
enum class UnverifiedReason : uint8_t {
	NONE              = 0x00,
	SOURCE_UNKNOWN    = 0x01,
	SIGNATURE_INVALID = 0x02
};

//=============================================================================
// Peer States (for LXMPeer)
//=============================================================================
enum class PeerState : uint8_t {
	IDLE                  = 0x00,
	LINK_ESTABLISHING     = 0x01,
	LINK_READY            = 0x02,
	REQUEST_SENT          = 0x03,
	RESPONSE_RECEIVED     = 0x04,
	RESOURCE_TRANSFERRING = 0x05
};

//=============================================================================
// Message Field IDs (for interoperability)
// Reference: LXMF/LXMF.py lines 8-41
//=============================================================================
namespace Fields {
	static constexpr uint8_t EMBEDDED_LXMS     = 0x01;
	static constexpr uint8_t TELEMETRY         = 0x02;
	static constexpr uint8_t TELEMETRY_STREAM  = 0x03;
	static constexpr uint8_t ICON_APPEARANCE   = 0x04;
	static constexpr uint8_t FILE_ATTACHMENTS  = 0x05;
	static constexpr uint8_t IMAGE             = 0x06;
	static constexpr uint8_t AUDIO             = 0x07;
	static constexpr uint8_t THREAD            = 0x08;
	static constexpr uint8_t COMMANDS          = 0x09;
	static constexpr uint8_t RESULTS           = 0x0A;
	static constexpr uint8_t GROUP             = 0x0B;
	static constexpr uint8_t TICKET            = 0x0C;
	static constexpr uint8_t EVENT             = 0x0D;
	static constexpr uint8_t RNR_REFS          = 0x0E;
	static constexpr uint8_t RENDERER          = 0x0F;

	// Custom fields
	static constexpr uint8_t CUSTOM_TYPE       = 0xFB;
	static constexpr uint8_t CUSTOM_DATA       = 0xFC;
	static constexpr uint8_t CUSTOM_META       = 0xFD;

	// Debug/non-specific
	static constexpr uint8_t NON_SPECIFIC      = 0xFE;
	static constexpr uint8_t DEBUG             = 0xFF;
}

//=============================================================================
// Audio Modes for FIELD_AUDIO
// Reference: LXMF/LXMF.py lines 55-79
//=============================================================================
namespace AudioMode {
	// Codec2 modes
	static constexpr uint8_t CODEC2_450PWB = 0x01;
	static constexpr uint8_t CODEC2_450    = 0x02;
	static constexpr uint8_t CODEC2_700C   = 0x03;
	static constexpr uint8_t CODEC2_1200   = 0x04;
	static constexpr uint8_t CODEC2_1300   = 0x05;
	static constexpr uint8_t CODEC2_1400   = 0x06;
	static constexpr uint8_t CODEC2_1600   = 0x07;
	static constexpr uint8_t CODEC2_2400   = 0x08;
	static constexpr uint8_t CODEC2_3200   = 0x09;

	// Opus modes
	static constexpr uint8_t OPUS_OGG       = 0x10;
	static constexpr uint8_t OPUS_LBW       = 0x11;
	static constexpr uint8_t OPUS_MBW       = 0x12;
	static constexpr uint8_t OPUS_PTT       = 0x13;
	static constexpr uint8_t OPUS_RT_HDX    = 0x14;
	static constexpr uint8_t OPUS_RT_FDX    = 0x15;
	static constexpr uint8_t OPUS_STANDARD  = 0x16;
	static constexpr uint8_t OPUS_HQ        = 0x17;
	static constexpr uint8_t OPUS_BROADCAST = 0x18;
	static constexpr uint8_t OPUS_LOSSLESS  = 0x19;

	// Custom
	static constexpr uint8_t CUSTOM         = 0xFF;
}

//=============================================================================
// Renderer Specifications for FIELD_RENDERER
// Reference: LXMF/LXMF.py lines 89-93
//=============================================================================
namespace Renderer {
	static constexpr uint8_t PLAIN    = 0x00;
	static constexpr uint8_t MICRON   = 0x01;
	static constexpr uint8_t MARKDOWN = 0x02;
	static constexpr uint8_t BBCODE   = 0x03;
}

//=============================================================================
// Propagation Node Metadata Field IDs
// Reference: LXMF/LXMF.py lines 98-104
//=============================================================================
namespace PNMeta {
	static constexpr uint8_t VERSION        = 0x00;
	static constexpr uint8_t NAME           = 0x01;
	static constexpr uint8_t SYNC_STRATUM   = 0x02;
	static constexpr uint8_t SYNC_THROTTLE  = 0x03;
	static constexpr uint8_t AUTH_BAND      = 0x04;
	static constexpr uint8_t UTIL_PRESSURE  = 0x05;
	static constexpr uint8_t CUSTOM         = 0xFF;
}

//=============================================================================
// Timing Constants
//=============================================================================
namespace Timing {
	// Message expiry (30 days in seconds)
	static constexpr double MESSAGE_EXPIRY = 30.0 * 24.0 * 60.0 * 60.0;

	// Ticket timing
	static constexpr double TICKET_EXPIRY   = 21.0 * 24.0 * 60.0 * 60.0;  // 21 days
	static constexpr double TICKET_GRACE    = 5.0 * 24.0 * 60.0 * 60.0;   // 5 days
	static constexpr double TICKET_RENEW    = 14.0 * 24.0 * 60.0 * 60.0;  // 14 days
	static constexpr double TICKET_INTERVAL = 1.0 * 24.0 * 60.0 * 60.0;   // 1 day

	// Router timing
	static constexpr double DELIVERY_RETRY_WAIT = 10.0;   // Seconds between retries
	static constexpr double PATH_REQUEST_WAIT   = 7.0;    // Seconds to wait for path
	static constexpr double LINK_MAX_INACTIVITY = 600.0;  // 10 minutes

	// Peer timing
	static constexpr double PEER_MAX_UNREACHABLE = 14.0 * 24.0 * 60.0 * 60.0;  // 14 days
	static constexpr double PEER_SYNC_BACKOFF    = 12.0 * 60.0;                 // 12 minutes
}

//=============================================================================
// Router Configuration
//=============================================================================
struct RouterConfig {
	size_t max_outbound_queue = 100;
	size_t max_delivery_attempts = 5;
	size_t max_peers = 20;
	size_t max_dedup_entries = 1000;

	// Propagation node settings
	bool enable_propagation = false;
	size_t propagation_limit = 256;       // Messages per transfer
	size_t propagation_transfer_limit = 256;  // KB per transfer
	size_t message_storage_limit = 0;     // 0 = unlimited
};

//=============================================================================
// Forward Declarations
//=============================================================================
class LXMessage;
class LXMRouter;
class LXMPeer;

//=============================================================================
// Callback Types
//=============================================================================
namespace Callbacks {
	// Called when message delivery state changes
	using MessageStateChanged = std::function<void(LXMessage& message, MessageState state)>;

	// Called when a message is received
	using MessageReceived = std::function<void(LXMessage& message)>;

	// Called when propagation node state changes
	using PropagationStateChanged = std::function<void(bool active)>;
}

}} // namespace RNS::LXMF
