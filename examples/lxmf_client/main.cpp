/*
##########################################################
# This example demonstrates LXMF messaging with          #
# microReticulum, compatible with Python LXMF clients.   #
##########################################################
*/

#include <AutoInterface.h>
#include <TCPClientInterface.h>

#include <Reticulum.h>
#include <Interface.h>
#include <Link.h>
#include <Identity.h>
#include <Destination.h>
#include <Packet.h>
#include <Transport.h>
#include <Log.h>
#include <Bytes.h>
#include <Type.h>
#include <Utilities/OS.h>

#include "LXMF/LXMFTypes.h"
#include "LXMF/LXMessage.h"
#include "LXMF/LXMRouter.h"

#ifdef ARDUINO
#include <Arduino.h>
#else
#include <termios.h>
#include <fcntl.h>
#include <stdio.h>
#include <signal.h>
#endif

#include <stdlib.h>
#include <unistd.h>
#include <string>
#include <iostream>

using namespace RNS;
using namespace RNS::LXMF;

// Global state
static LXMRouter* router = nullptr;
static AutoInterface* auto_interface = nullptr;
static TCPClientInterface* tcp_interface = nullptr;
static Interface registered_interface = {Type::NONE};  // Interface wrapper for Transport
static Destination delivery_destination = {Type::NONE};
static Identity local_identity = {Type::NONE};
static bool running = true;
static bool use_tcp = false;  // Set via --tcp flag or when AutoInterface fails
static bool use_opportunistic = false;  // Set via --opportunistic flag

// Destination hash to send to (set via command line or hardcoded)
static std::string target_dest_hash;

//=============================================================================
// Callbacks
//=============================================================================

void message_received(const LXMessage& message) {
    std::cout << "\n========================================" << std::endl;
    std::cout << "MESSAGE RECEIVED" << std::endl;
    std::cout << "  Hash: " << message.hash().toHex() << std::endl;
    std::cout << "  From: " << message.source_hash().toHex() << std::endl;
    std::cout << "  To:   " << message.destination_hash().toHex() << std::endl;
    std::cout << "  Title: " << message.title_as_string() << std::endl;
    std::cout << "  Content: " << message.content_as_string() << std::endl;
    std::cout << "  Timestamp: " << message.timestamp() << std::endl;
    std::cout << "  Method: " << static_cast<int>(message.method()) << std::endl;
    std::cout << "  Signature Valid: " << (message.signature_validated() ? "Yes" : "No") << std::endl;
    std::cout << "========================================\n" << std::endl;
}

// LXMF Announce Handler
class LXMFAnnounceHandler : public AnnounceHandler {
public:
    LXMFAnnounceHandler() : AnnounceHandler("lxmf.delivery") {}

    void received_announce(const Bytes& destination_hash, const Identity& announced_identity, const Bytes& app_data) override {
        std::cout << "\n[ANNOUNCE] LXMF destination: " << destination_hash.toHex() << std::endl;

        // Remember the identity for later use
        Identity::remember(Bytes(), destination_hash, announced_identity.get_public_key(), app_data);

        // Parse app_data if present (msgpack [display_name, stamp_cost])
        if (app_data.size() > 0) {
            std::cout << "  App Data: " << app_data.toHex() << std::endl;
        }
    }
};

//=============================================================================
// Send Message
//=============================================================================

bool send_message(const std::string& dest_hash_hex, const std::string& content, const std::string& title = "") {
    if (!router || !delivery_destination) {
        std::cerr << "Router not initialized" << std::endl;
        return false;
    }

    // Parse destination hash
    Bytes dest_hash;
    dest_hash.assignHex(dest_hash_hex.c_str());
    if (dest_hash.size() != 16) {
        std::cerr << "Invalid destination hash: " << dest_hash_hex << std::endl;
        return false;
    }

    // Try to recall identity
    Identity target_identity = Identity::recall(dest_hash);
    if (!target_identity) {
        std::cout << "Unknown identity for " << dest_hash_hex << ", requesting path..." << std::endl;
        Transport::request_path(dest_hash);

        // Wait and retry
        for (int i = 0; i < 10; i++) {
            Utilities::OS::sleep(0.5);
            target_identity = Identity::recall(dest_hash);
            if (target_identity) break;
        }

        if (!target_identity) {
            std::cerr << "Could not recall identity for destination" << std::endl;
            return false;
        }
    }

    std::cout << "Creating destination for " << dest_hash_hex << std::endl;

    // Create destination
    Destination target_dest(
        target_identity,
        Type::Destination::OUT,
        Type::Destination::SINGLE,
        LXMF::APP_NAME,
        LXMF::ASPECT_DELIVERY
    );

    if (!target_dest) {
        std::cerr << "Failed to create destination" << std::endl;
        return false;
    }

    // Create message
    DeliveryMethod method = use_opportunistic ? DeliveryMethod::OPPORTUNISTIC : DeliveryMethod::DIRECT;
    std::cout << "Using delivery method: " << (use_opportunistic ? "OPPORTUNISTIC" : "DIRECT") << std::endl;

    LXMessage message = LXMessage::create(
        target_dest,
        delivery_destination,
        content,
        title,
        {},
        method
    );

    if (!message) {
        std::cerr << "Failed to create message" << std::endl;
        return false;
    }

    // Queue for delivery
    if (!router->handle_outbound(message)) {
        std::cerr << "Failed to queue message" << std::endl;
        return false;
    }

    std::cout << "Message queued for delivery" << std::endl;
    std::cout << "  Hash: " << message.hash().toHex() << std::endl;
    std::cout << "  To: " << dest_hash_hex << std::endl;

    return true;
}

//=============================================================================
// Signal Handler
//=============================================================================

#ifndef ARDUINO
void signal_handler(int sig) {
    std::cout << "\nShutting down..." << std::endl;
    running = false;
}
#endif

//=============================================================================
// Main
//=============================================================================

#ifdef ARDUINO
void setup() {
    Serial.begin(115200);
    delay(2000);
#else
int main(int argc, char* argv[]) {
    // Parse command line
    std::string tcp_host = "127.0.0.1";
    int tcp_port = 4242;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--tcp") {
            use_tcp = true;
        } else if (arg.find("--tcp-host=") == 0) {
            tcp_host = arg.substr(11);
            use_tcp = true;
        } else if (arg.find("--tcp-port=") == 0) {
            tcp_port = std::stoi(arg.substr(11));
            use_tcp = true;
        } else if (arg == "--opportunistic") {
            use_opportunistic = true;
        } else if (arg[0] != '-') {
            target_dest_hash = arg;
        }
    }

    // Set up signal handler
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
#endif

    std::cout << "========================================" << std::endl;
    std::cout << "microReticulum LXMF Client" << std::endl;
    std::cout << "========================================" << std::endl;

    // Initialize logging - use LOG_DEBUG (7) to see all TCP debug messages
    RNS::loglevel(RNS::LOG_DEBUG);

    // Initialize Reticulum
    std::cout << "\nInitializing Reticulum..." << std::endl;
    Reticulum reticulum;

    // Create or load identity
    // For testing, create a new identity each time
    local_identity = Identity();
    std::cout << "Identity hash: " << local_identity.hash().toHex() << std::endl;

    // Add interface
#ifndef ARDUINO
    if (use_tcp) {
        // Use TCP interface to connect to Python RNS
        std::cout << "Adding TCPClientInterface to " << tcp_host << ":" << tcp_port << "..." << std::endl;
        tcp_interface = new TCPClientInterface("TCP");
        tcp_interface->set_target_host(tcp_host);
        tcp_interface->set_target_port(tcp_port);
        if (!tcp_interface->start()) {
            std::cerr << "Failed to start TCPClientInterface" << std::endl;
            return 1;
        }
        std::cout << "TCPClientInterface started successfully" << std::endl;

        // Register interface with Transport (use global to avoid dangling pointer)
        registered_interface = Interface(tcp_interface);
        Transport::register_interface(registered_interface);
        std::cout << "TCPClientInterface registered with Transport" << std::endl;
    } else
#endif
    {
        // Use AutoInterface for multicast discovery
        std::cout << "Adding AutoInterface..." << std::endl;
        auto_interface = new AutoInterface("Auto");
        if (!auto_interface->start()) {
            std::cerr << "Failed to start AutoInterface" << std::endl;
#ifdef ARDUINO
            return;
#else
            return 1;
#endif
        }
        std::cout << "AutoInterface started successfully" << std::endl;

        // Register interface with Transport (use global to avoid dangling pointer)
        registered_interface = Interface(auto_interface);
        Transport::register_interface(registered_interface);
        std::cout << "AutoInterface registered with Transport" << std::endl;
    }

    // Set Transport identity (for native builds without filesystem, skip reticulum.start())
    // This is required for Transport to process incoming packets
    Identity transport_identity;
    Transport::identity(transport_identity);
    std::cout << "Transport identity set: " << transport_identity.hash().toHex() << std::endl;

    // Create LXMF router
    std::cout << "\nInitializing LXMF Router..." << std::endl;
    router = new LXMRouter();

    // Register delivery identity
    delivery_destination = router->register_delivery_identity(local_identity, "microReticulum LXMF Client");
    if (!delivery_destination) {
        std::cerr << "Failed to register delivery identity" << std::endl;
#ifdef ARDUINO
        return;
#else
        return 1;
#endif
    }

    std::cout << "Delivery destination: " << delivery_destination.hash().toHex() << std::endl;

    // Register callbacks
    router->register_delivery_callback(message_received);

    // Register announce handler for LXMF destinations
    auto announce_handler = std::make_shared<LXMFAnnounceHandler>();
    Transport::register_announce_handler(announce_handler);

    // Announce ourselves
    std::cout << "\nAnnouncing LXMF destination..." << std::endl;
    router->announce();

    std::cout << "\n========================================" << std::endl;
    std::cout << "LXMF Client Ready" << std::endl;
    std::cout << "  Destination: " << delivery_destination.hash().toHex() << std::endl;
    std::cout << "========================================\n" << std::endl;

    // If target specified, wait for identity and send a test message
    if (!target_dest_hash.empty()) {
        std::cout << "Will send message to: " << target_dest_hash << std::endl;

        // Parse destination hash
        Bytes dest_hash;
        dest_hash.assignHex(target_dest_hash.c_str());

        // Wait for identity to be known (max 15 seconds)
        std::cout << "Waiting for identity to be discovered..." << std::endl;
        Identity target_identity;
        for (int i = 0; i < 30; i++) {
            target_identity = Identity::recall(dest_hash);
            if (target_identity) {
                std::cout << "Identity discovered after " << (i * 0.5) << " seconds" << std::endl;
                break;
            }
            // Process interface to receive announces
            if (auto_interface) auto_interface->loop();
            if (tcp_interface) tcp_interface->loop();
            Utilities::OS::sleep(0.5);
        }

        if (!target_identity) {
            std::cout << "Warning: Could not discover identity, requesting path..." << std::endl;
            Transport::request_path(dest_hash);
            // Wait a bit more
            for (int i = 0; i < 20; i++) {
                if (auto_interface) auto_interface->loop();
                if (tcp_interface) tcp_interface->loop();
                Utilities::OS::sleep(0.5);
                target_identity = Identity::recall(dest_hash);
                if (target_identity) {
                    std::cout << "Identity discovered via path request" << std::endl;
                    break;
                }
            }
        }

        if (target_identity) {
            send_message(target_dest_hash, "Hello from microReticulum!", "Test Message");
        } else {
            std::cerr << "Failed to discover identity for " << target_dest_hash << std::endl;
        }
    }

    // Main loop
    std::cout << "\nRunning. Press Ctrl+C to stop.\n" << std::endl;

#ifdef ARDUINO
}

void loop() {
    // Process interface
    if (auto_interface) {
        auto_interface->loop();
    }
    // Process router
    if (router) {
        router->loop();
    }
    delay(100);
}
#else
    while (running) {
        // Process interface
        if (auto_interface) {
            auto_interface->loop();
        }
        if (tcp_interface) {
            tcp_interface->loop();
        }
        // Process router
        if (router) {
            router->loop();
        }

        // Small delay
        Utilities::OS::sleep(0.1);
    }

    // Cleanup
    delete router;
    router = nullptr;
    // Interface wrapper owns the InterfaceImpl via shared_ptr, so don't delete manually
    registered_interface.clear();
    auto_interface = nullptr;
    tcp_interface = nullptr;

    std::cout << "\nShutdown complete" << std::endl;
    return 0;
}
#endif
