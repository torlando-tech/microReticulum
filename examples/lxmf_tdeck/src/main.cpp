// LXMF Messenger for LilyGO T-Deck Plus
// Complete LXMF messaging application with LVGL UI

#include <Arduino.h>
#include <Wire.h>
#include <SPIFFS.h>

// Reticulum
#include <Reticulum.h>
#include <Utilities/OS.h>

// Filesystem
#include <UniversalFileSystem.h>
#include <Identity.h>
#include <Destination.h>
#include <Transport.h>
#include <Interface.h>

// TCP Client Interface
#include "TCPClientInterface.h"

// LXMF
#include <LXMF/LXMRouter.h>
#include <LXMF/MessageStore.h>

// Hardware drivers
#include <Hardware/TDeck/Config.h>
#include <Hardware/TDeck/Display.h>
#include <Hardware/TDeck/Keyboard.h>
#include <Hardware/TDeck/Touch.h>
#include <Hardware/TDeck/Trackball.h>

// UI
#include <UI/LVGL/LVGLInit.h>
#include <UI/LXMF/UIManager.h>

// Logging
#include <Log.h>

using namespace RNS;
using namespace LXMF;
using namespace Hardware::TDeck;

// Configuration
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* RNS_SERVER_IP = "amsterdam.connect.reticulum.network";
const uint16_t RNS_SERVER_PORT = 4965;

// Global instances
Reticulum* reticulum = nullptr;
Identity* identity = nullptr;
LXMRouter* router = nullptr;
MessageStore* message_store = nullptr;
UI::LXMF::UIManager* ui_manager = nullptr;
TCPClientInterface* tcp_interface_impl = nullptr;
Interface* tcp_interface = nullptr;

// Timing
uint32_t last_ui_update = 0;
uint32_t last_announce = 0;
const uint32_t ANNOUNCE_INTERVAL = 300000;  // 5 minutes

void setup_wifi() {
    String msg = "Connecting to WiFi: " + String(WIFI_SSID);
    INFO(msg.c_str());

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 30000) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        INFO("WiFi connected!");
        msg = "  IP address: " + WiFi.localIP().toString();
        INFO(msg.c_str());
        msg = "  RSSI: " + String(WiFi.RSSI()) + " dBm";
        INFO(msg.c_str());
    } else {
        ERROR("WiFi connection failed!");
    }
}

void setup_hardware() {
    INFO("\n=== Hardware Initialization ===");

    // Initialize SPIFFS for persistence
    if (!SPIFFS.begin(true)) {
        ERROR("SPIFFS mount failed!");
    } else {
        INFO("SPIFFS mounted");

        // Register filesystem with Reticulum OS utilities
        static RNS::FileSystem fs = new UniversalFileSystem();
        fs.init();
        RNS::Utilities::OS::register_filesystem(fs);
        INFO("Filesystem registered");
    }

    // Initialize I2C for keyboard and touch
    Wire.begin(Pin::I2C_SDA, Pin::I2C_SCL);
    Wire.setClock(I2C::FREQUENCY);
    INFO("I2C initialized");

    // Initialize power
    pinMode(Pin::POWER_EN, OUTPUT);
    digitalWrite(Pin::POWER_EN, HIGH);
    INFO("Power enabled");
}

void setup_lvgl_and_ui() {
    INFO("\n=== LVGL & UI Initialization ===");

    // Initialize LVGL with all hardware drivers
    if (!UI::LVGL::LVGLInit::init()) {
        ERROR("LVGL initialization failed!");
        while (1) delay(1000);
    }

    INFO("LVGL initialized");
}

void setup_reticulum() {
    INFO("\n=== Reticulum Initialization ===");

    // Create Reticulum instance (no auto-init)
    reticulum = new Reticulum();

    // Load or create identity
    String identity_path = "/identity";
    if (SPIFFS.exists(identity_path)) {
        INFO("Loading identity from storage");
        identity = new Identity(identity_path.c_str());
    } else {
        INFO("Creating new identity");
        identity = new Identity();
        identity->to_file(identity_path.c_str());
    }

    std::string identity_hex = identity->get_public_key().toHex().substr(0, 16);
    std::string msg = "  Identity: " + identity_hex + "...";
    INFO(msg.c_str());

    // Add TCP client interface
    String server_addr = String(RNS_SERVER_IP) + ":" + String(RNS_SERVER_PORT);
    msg = std::string("Connecting to RNS server at ") + server_addr.c_str();
    INFO(msg.c_str());

    tcp_interface_impl = new TCPClientInterface("tcp0");
    tcp_interface_impl->set_target_host(RNS_SERVER_IP);
    tcp_interface_impl->set_target_port(RNS_SERVER_PORT);
    tcp_interface = new Interface(tcp_interface_impl);

    if (!tcp_interface->start()) {
        ERROR("Failed to connect to RNS server!");
    } else {
        INFO("Connected to RNS server");
        Transport::register_interface(*tcp_interface);
    }
}

void setup_lxmf() {
    INFO("\n=== LXMF Initialization ===");

    // Create message store
    message_store = new MessageStore("/lxmf");
    INFO("Message store ready");

    // Create LXMF router
    router = new LXMRouter(*identity, "/lxmf");
    INFO("LXMF router created");

    // Announce delivery destination
    router->announce();
    last_announce = millis();

    std::string dest_hash = router->delivery_destination().hash().toHex();
    std::string msg = "  Delivery destination: " + dest_hash;
    INFO(msg.c_str());
}

void setup_ui_manager() {
    INFO("\n=== UI Manager Initialization ===");

    // Create UI manager
    ui_manager = new UI::LXMF::UIManager(*reticulum, *router, *message_store);

    if (!ui_manager->init()) {
        ERROR("UI manager initialization failed!");
        while (1) delay(1000);
    }

    INFO("UI manager ready");
}

void setup() {
    // Initialize serial
    Serial.begin(115200);
    delay(2000);

    INFO("\n");
    INFO("╔══════════════════════════════════════╗");
    INFO("║   LXMF Messenger for T-Deck Plus    ║");
    INFO("║   microReticulum + LVGL UI          ║");
    INFO("╚══════════════════════════════════════╝");
    INFO("");

    // Initialize hardware
    setup_hardware();

    // Initialize WiFi
    setup_wifi();

    // Initialize LVGL and hardware drivers
    setup_lvgl_and_ui();

    // Initialize Reticulum
    setup_reticulum();

    // Initialize LXMF
    setup_lxmf();

    // Initialize UI manager
    setup_ui_manager();

    INFO("\n");
    INFO("╔══════════════════════════════════════╗");
    INFO("║     System Ready - Enjoy!            ║");
    INFO("╚══════════════════════════════════════╝");
    INFO("");

    // Show startup message
    INFO("Press any key to start messaging");
}

void loop() {
    // Handle LVGL rendering (must be called frequently for smooth UI)
    UI::LVGL::LVGLInit::task_handler();

    // Process Reticulum
    reticulum->loop();

    // Process TCP interface
    if (tcp_interface) {
        tcp_interface->loop();
    }

    // Update UI manager (processes LXMF messages)
    if (ui_manager) {
        ui_manager->update();
    }

    // Periodic announce
    if (millis() - last_announce > ANNOUNCE_INTERVAL) {
        if (router) {
            router->announce();
            last_announce = millis();
            TRACE("Periodic announce sent");
        }
    }

    // Small delay to prevent tight loop
    delay(5);
}
