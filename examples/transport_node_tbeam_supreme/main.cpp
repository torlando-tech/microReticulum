/*
##########################################################
# Transport Node for T-Beam Supreme                      #
#                                                        #
# Full Reticulum transport node with:                    #
# - AutoInterface (IPv6 multicast peer discovery)        #
# - BLEInterface (BLE mesh networking)                   #
# - TCPClientInterface (backup to Python RNS)            #
# - Probe support (responds to rnprobe)                  #
# - Display support                                      #
# - Full Link/Resource/Channel/Buffer stack              #
##########################################################
*/

#include <Arduino.h>
#include <SPIFFS.h>
#include <WiFi.h>

// Interfaces
#include <TCPClientInterface.h>
#include <AutoInterface.h>
#include <BLEInterface.h>
#include "../common/tcp_interface/tcp_config.h"
#include "../common/udp_interface/wifi_credentials.h"

// WiFi connection utility (separate from TCPClientInterface)
bool wifi_connect() {
    RNS::log("Connecting to WiFi: " + std::string(WIFI_SSID));

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40) {  // 20 second timeout
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() != WL_CONNECTED) {
        RNS::error("WiFi connection FAILED!");
        return false;
    }

    Serial.println();
    RNS::log("WiFi connected: " + std::string(WiFi.localIP().toString().c_str()));
    return true;
}

// Filesystem
#include <UniversalFileSystem.h>

// Reticulum core
#include <Reticulum.h>
#include <Interface.h>
#include <Transport.h>
#include <Identity.h>
#include <Destination.h>
#include <Packet.h>
#include <Link.h>
#include <Resource.h>
#include <Channel.h>
#include <Buffer.h>
#include <Log.h>
#include <Bytes.h>
#include <Type.h>
#include <Utilities/OS.h>

// Display
#ifdef HAS_DISPLAY
#include <Display.h>
#endif

// Application name for destinations
#define APP_NAME "transport_node"

// Global objects
RNS::Reticulum reticulum;
RNS::Identity node_identity({RNS::Type::NONE});
RNS::Destination node_destination({RNS::Type::NONE});
RNS::Interface auto_interface({RNS::Type::NONE});
RNS::Interface tcp_interface({RNS::Type::NONE});
RNS::Interface ble_interface({RNS::Type::NONE});
BLEInterface* ble_iface = nullptr;  // Keep pointer for identity setup
RNS::Link active_link({RNS::Type::NONE});

// Link callbacks
void on_link_established(RNS::Link& link) {
    RNS::log("Link established from remote peer");
    active_link = link;

    // Set up link callbacks
    link.set_link_closed_callback([](RNS::Link& l) {
        RNS::log("Link closed");
        active_link = RNS::Link({RNS::Type::NONE});
    });

    link.set_packet_callback([](const RNS::Bytes& data, const RNS::Packet& packet) {
        RNS::log("Received packet on link: " + data.toString());
        // Echo back
        if (active_link) {
            RNS::Packet reply(active_link, RNS::Bytes("Echo: " + data.toString()));
            reply.send();
        }
    });

    // Set up resource callbacks
    link.set_resource_started_callback([](const RNS::Resource& resource) {
        RNS::log("Resource transfer started, size: " + std::to_string(resource.size()));
    });

    link.set_resource_concluded_callback([](const RNS::Resource& resource) {
        if (resource.status() == RNS::Type::Resource::COMPLETE) {
            RNS::log("Resource received: " + std::to_string(resource.size()) + " bytes");
        }
    });
}

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000); // Wait up to 3s for serial

    RNS::loglevel(RNS::LOG_DEBUG);
    RNS::log("=== microReticulum Transport Node ===");
    RNS::log("T-Beam Supreme Edition");

    // Initialize display first (before other I2C devices)
    #ifdef HAS_DISPLAY
    if (RNS::Display::init()) {
        RNS::log("Display initialized");
    } else {
        RNS::log("Display init failed (continuing without)");
    }
    #endif

    // Initialize filesystem
    if (!SPIFFS.begin(true)) {
        RNS::error("SPIFFS initialization failed!");
        return;
    }
    RNS::FileSystem fs = new UniversalFileSystem();
    fs.init();
    RNS::Utilities::OS::register_filesystem(fs);
    RNS::log("Filesystem initialized");

    // Enable probe support - allows rnprobe to reach this node
    RNS::Reticulum::probe_destination_enabled(true);
    RNS::Transport::probe_destination_enabled(true);
    RNS::log("Probe support enabled");

    // Connect WiFi first (required for AutoInterface)
    if (!wifi_connect()) {
        RNS::error("Cannot start without WiFi!");
        return;
    }

    // AUTOINTERFACE-ONLY TEST MODE
    // TCPClientInterface disabled for isolated testing
    #if 0
    TCPClientInterface* tcp_iface = new TCPClientInterface("TCP");
    tcp_iface->set_target_host(TCP_SERVER_HOST);
    tcp_iface->set_target_port(TCP_SERVER_PORT);
    tcp_interface = tcp_iface;
    tcp_interface.mode(RNS::Type::Interface::MODE_GATEWAY);
    RNS::Transport::register_interface(tcp_interface);
    tcp_interface.start();
    RNS::log("TCPClientInterface started (MODE_GATEWAY) -> " +
             std::string(TCP_SERVER_HOST) + ":" + std::to_string(TCP_SERVER_PORT));
    #endif
    RNS::log("*** AUTOINTERFACE-ONLY TEST MODE ***");

    // Initialize AutoInterface (primary - IPv6 multicast discovery)
    AutoInterface* auto_iface = new AutoInterface("Auto");
    auto_interface = auto_iface;
    auto_interface.mode(RNS::Type::Interface::MODE_FULL);
    RNS::Transport::register_interface(auto_interface);
    auto_interface.start();
    RNS::log("AutoInterface started (MODE_FULL)");

    // Initialize BLEInterface (BLE mesh networking)
    ble_iface = new BLEInterface("BLE");
    ble_iface->setDeviceName("TBS-Node");
    ble_iface->setRole(RNS::BLE::Role::DUAL);
    ble_interface = ble_iface;
    ble_interface.mode(RNS::Type::Interface::MODE_FULL);
    RNS::Transport::register_interface(ble_interface);
    ble_interface.start();
    RNS::log("BLEInterface started (MODE_FULL, dual-mode)");

    // Start Reticulum
    reticulum.start();
    RNS::log("Reticulum started");

    // Create node identity and destination
    node_identity = RNS::Identity();
    node_destination = RNS::Destination(
        node_identity,
        RNS::Type::Destination::IN,
        RNS::Type::Destination::SINGLE,
        APP_NAME,
        "node"
    );
    node_destination.set_link_established_callback(on_link_established);
    node_destination.set_proof_strategy(RNS::Type::Destination::PROVE_ALL);

    // Set BLE local identity (first 16 bytes of identity hash for handshake)
    if (ble_iface) {
        RNS::Bytes ble_id = node_identity.hash();
        ble_id.resize(16);
        ble_iface->setLocalIdentity(ble_id);
    }

    RNS::log("Transport Node ready");
    RNS::log("  Destination: " + node_destination.hash().toHex());
    RNS::log("  Identity:    " + node_identity.hash().toHex().substr(0, 12) + "...");

    // Configure display with data sources
    #ifdef HAS_DISPLAY
    RNS::Display::set_identity(node_identity);
    RNS::Display::set_interface(&auto_interface);  // Show AutoInterface status
    RNS::Display::set_reticulum(&reticulum);
    #endif

    // Send initial announce
    node_destination.announce();
    RNS::log("Initial announce sent");
    RNS::log("");
    RNS::log("Press Enter via Serial to send announce");
}

void loop() {
    // Run Reticulum event loop
    reticulum.loop();

    // Update display
    #ifdef HAS_DISPLAY
    RNS::Display::update();
    #endif

    // Handle Serial input for manual announce
    while (Serial.available() > 0) {
        char ch = Serial.read();
        if (ch == '\n' || ch == '\r') {
            if (node_destination) {
                node_destination.announce();
                RNS::log("Sent announce from " + node_destination.hash().toHex());
            }
        }
    }

    delay(10);
}
