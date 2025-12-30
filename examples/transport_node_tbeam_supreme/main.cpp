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

// TEMPORARY MINIMAL TEST - uncomment to use minimal test only
// #define MINIMAL_SERIAL_TEST
// #define MINIMAL_BLE_TEST
// #define MINIMAL_PMU_TEST

#ifdef MINIMAL_SERIAL_TEST
#include <Arduino.h>

// Minimal test - just Serial, no USB.h needed when CDC_ON_BOOT is set
void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 5000) {
        delay(10);  // Wait up to 5 seconds for USB CDC
    }
    Serial.println("=== ESP32-S3 T-Beam Supreme ===");
    Serial.println("Serial test started");
}

void loop() {
    Serial.print("Uptime: ");
    Serial.println(millis() / 1000);
    delay(2000);
}

#elif defined(MINIMAL_BLE_TEST)
// NimBLE test with GATT server and service UUID advertising
#include <Arduino.h>
#include <Wire.h>
#include <XPowersLib.h>
#include <NimBLEDevice.h>
#include "esp_bt.h"

// T-Beam Supreme I2C pins
#define PMU_SDA 42
#define PMU_SCL 41

// Reticulum BLE Service UUIDs
#define SERVICE_UUID        "37145b00-442d-4a94-917f-8f42c5da28e3"
#define TX_CHAR_UUID        "37145b00-442d-4a94-917f-8f42c5da28e4"
#define RX_CHAR_UUID        "37145b00-442d-4a94-917f-8f42c5da28e5"
#define IDENTITY_CHAR_UUID  "37145b00-442d-4a94-917f-8f42c5da28e6"

XPowersLibInterface* PMU = nullptr;
NimBLEServer* pServer = nullptr;
NimBLEService* pService = nullptr;
NimBLECharacteristic* pTxChar = nullptr;
NimBLECharacteristic* pRxChar = nullptr;
NimBLECharacteristic* pIdChar = nullptr;

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
        Serial.printf("Client connected: %s\n", connInfo.getAddress().toString().c_str());
    }
    void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
        Serial.printf("Client disconnected, reason: %d\n", reason);
        // Restart advertising
        NimBLEDevice::getAdvertising()->start();
    }
};

bool initPMU() {
    Wire1.begin(PMU_SDA, PMU_SCL);
    XPowersAXP2101* pmu = new XPowersAXP2101(Wire1);
    if (!pmu->init()) {
        delete pmu;
        return false;
    }
    PMU = pmu;

    // Power cycle on cold boot
    if (ESP_SLEEP_WAKEUP_UNDEFINED == esp_sleep_get_wakeup_cause()) {
        PMU->disablePowerOutput(XPOWERS_ALDO1);
        PMU->disablePowerOutput(XPOWERS_ALDO2);
        PMU->disablePowerOutput(XPOWERS_BLDO1);
        delay(250);
    }

    // Enable power rails
    PMU->setPowerChannelVoltage(XPOWERS_ALDO1, 3300);
    PMU->enablePowerOutput(XPOWERS_ALDO1);
    PMU->setPowerChannelVoltage(XPOWERS_ALDO2, 3300);
    PMU->enablePowerOutput(XPOWERS_ALDO2);
    PMU->setPowerChannelVoltage(XPOWERS_ALDO3, 3300);
    PMU->enablePowerOutput(XPOWERS_ALDO3);
    PMU->setPowerChannelVoltage(XPOWERS_ALDO4, 3300);
    PMU->enablePowerOutput(XPOWERS_ALDO4);

    return true;
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("=== MINIMAL BLE TEST with Service UUID ===");

    // Initialize PMU first
    Serial.println("Initializing PMU...");
    if (!initPMU()) {
        Serial.println("PMU init FAILED!");
    } else {
        Serial.println("PMU init OK");
    }

    // Release memory from classic BT if present
    esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);

    // Initialize NimBLE
    Serial.println("Initializing NimBLE...");
    if (!NimBLEDevice::init("TBS-Test")) {
        Serial.println("NimBLE init FAILED!");
        while(1) delay(100);
    }
    Serial.println("NimBLE init OK");

    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    NimBLEDevice::setMTU(517);

    // Create GATT server
    Serial.println("Creating GATT server...");
    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());

    // Create Reticulum service
    Serial.println("Creating service...");
    pService = pServer->createService(SERVICE_UUID);

    // Create characteristics
    pRxChar = pService->createCharacteristic(
        RX_CHAR_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );
    pTxChar = pService->createCharacteristic(
        TX_CHAR_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
    );
    pIdChar = pService->createCharacteristic(
        IDENTITY_CHAR_UUID,
        NIMBLE_PROPERTY::READ
    );
    // Set a test identity value
    uint8_t testId[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                          0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};
    pIdChar->setValue(testId, 16);

    // Start service
    Serial.println("Starting service...");
    pService->start();

    // Configure advertising with service UUID
    Serial.println("Configuring advertising...");
    NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();

    // Method: Set service UUID via the advertising object's service UUID list
    // Then let NimBLE build the advertising data itself
    pAdv->reset();  // Clear any prior state
    pAdv->addServiceUUID(NimBLEUUID(SERVICE_UUID));
    pAdv->setName("TBS-Test");

    Serial.printf("Service UUID bytes: ");
    NimBLEUUID uuid(SERVICE_UUID);
    const uint8_t* uuidBytes = uuid.getValue();
    for (int i = 0; i < 16; i++) {
        Serial.printf("%02X ", uuidBytes[i]);
    }
    Serial.println();

    // Start advertising
    Serial.println("Starting advertising...");
    bool advResult = pAdv->start();
    Serial.printf("pAdv->start() returned: %s\n", advResult ? "true" : "false");
    Serial.printf("pAdv->isAdvertising(): %s\n", pAdv->isAdvertising() ? "true" : "false");

    if (!advResult) {
        Serial.println("Advertising FAILED to start!");
        while(1) delay(50);
    }

    // Print our MAC address
    Serial.printf("Local BLE MAC: %s\n", NimBLEDevice::getAddress().toString().c_str());

    Serial.println("=== BLE READY ===");
    Serial.printf("Device: TBS-Test\n");
    Serial.printf("Service UUID: %s\n", SERVICE_UUID);
    Serial.println("Waiting for connections...");
}

void loop() {
    static uint32_t lastPrint = 0;
    if (millis() - lastPrint > 5000) {
        lastPrint = millis();
        Serial.printf("Uptime: %lu sec, Connected: %d\n",
                      millis()/1000, pServer->getConnectedCount());
    }
    delay(100);
}

#elif defined(MINIMAL_PMU_TEST)
// PMU + Display + BLE test for T-Beam Supreme
#include <Arduino.h>
#include <Wire.h>
#include <XPowersLib.h>
#include <Adafruit_SH110X.h>
#include <NimBLEDevice.h>

// T-Beam Supreme pins
#define I2C_SDA      17
#define I2C_SCL      18
#define I2C1_SDA     42
#define I2C1_SCL     41

// Use interface type like LilyGo does (public methods)
XPowersLibInterface *PMU = nullptr;
Adafruit_SH1106G display(128, 64, &Wire);
bool pmu_ok = false;
bool ble_ok = false;

void setup() {
    // Step 1: Init PMU I2C
    Wire1.begin(I2C1_SDA, I2C1_SCL);

    // Step 2: Init PMU
    XPowersAXP2101 *pmu = new XPowersAXP2101(Wire1);
    pmu_ok = pmu->init();
    if (pmu_ok) {
        PMU = pmu;
        // Power cycle on cold boot
        if (ESP_SLEEP_WAKEUP_UNDEFINED == esp_sleep_get_wakeup_cause()) {
            PMU->disablePowerOutput(XPOWERS_ALDO1);
            PMU->disablePowerOutput(XPOWERS_ALDO2);
            delay(250);
        }
        // Enable display power (ALDO1, ALDO2)
        PMU->setPowerChannelVoltage(XPOWERS_ALDO1, 3300);
        PMU->enablePowerOutput(XPOWERS_ALDO1);
        PMU->setPowerChannelVoltage(XPOWERS_ALDO2, 3300);
        PMU->enablePowerOutput(XPOWERS_ALDO2);
    } else {
        delete pmu;
    }

    delay(200);

    // Step 3: Init display I2C
    Wire.begin(I2C_SDA, I2C_SCL);
    delay(100);

    // Step 4: Init Display
    if (display.begin(0x3C, true)) {
        display.clearDisplay();
        display.setTextSize(2);
        display.setTextColor(SH110X_WHITE);
        display.setCursor(0, 0);
        display.println("microRNS");
        display.setTextSize(1);
        display.println();
        display.println(pmu_ok ? "PMU: OK" : "PMU: FAIL");
        display.display();
    }

    // Step 5: Init BLE
    ble_ok = NimBLEDevice::init("TBS-Test");
    display.println(ble_ok ? "BLE: OK" : "BLE: FAIL");
    display.display();

    if (ble_ok) {
        NimBLEDevice::setPower(ESP_PWR_LVL_P9);
        NimBLEServer* pServer = NimBLEDevice::createServer();
        NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();
        pAdv->setName("TBS-Test");
        pAdv->start();
        display.println("Advertising...");
        display.display();
    }
}

void loop() {
    static int cnt = 0;
    display.fillRect(0, 54, 128, 10, SH110X_BLACK);
    display.setCursor(0, 54);
    display.print("Up: ");
    display.print(cnt++);
    display.print("s");
    display.display();
    delay(1000);
}

#else

#include <Arduino.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <Wire.h>

// PMU (Power Management Unit) - AXP2101
#include <XPowersLib.h>

// T-Beam Supreme I2C pins
#define I2C_SDA      17   // Display/sensors I2C
#define I2C_SCL      18
#define PMU_SDA      42   // PMU I2C
#define PMU_SCL      41
#define PMU_IRQ      40

// Use interface type for public method access (LilyGo pattern)
XPowersLibInterface *PMU = nullptr;

// Initialize PMU - MUST be done first to power on peripherals
bool pmu_init() {
    // Initialize PMU I2C bus (separate from display I2C)
    Wire1.begin(PMU_SDA, PMU_SCL);

    // Use concrete type for init, interface for subsequent calls
    XPowersAXP2101 *pmu = new XPowersAXP2101(Wire1);
    if (!pmu->init()) {
        Serial.println("PMU init FAILED!");
        delete pmu;
        return false;
    }
    PMU = pmu;

    // Power cycle on cold boot (LilyGo pattern)
    if (ESP_SLEEP_WAKEUP_UNDEFINED == esp_sleep_get_wakeup_cause()) {
        PMU->disablePowerOutput(XPOWERS_ALDO1);
        PMU->disablePowerOutput(XPOWERS_ALDO2);
        PMU->disablePowerOutput(XPOWERS_BLDO1);
        delay(250);
    }

    // Enable all power rails at 3.3V (T-Beam Supreme configuration)
    // ALDO1: Sensors (IMU, magnetometer, BME280) and OLED display
    PMU->setPowerChannelVoltage(XPOWERS_ALDO1, 3300);
    PMU->enablePowerOutput(XPOWERS_ALDO1);

    // ALDO2: Sensor communication
    PMU->setPowerChannelVoltage(XPOWERS_ALDO2, 3300);
    PMU->enablePowerOutput(XPOWERS_ALDO2);

    // ALDO3: LoRa radio
    PMU->setPowerChannelVoltage(XPOWERS_ALDO3, 3300);
    PMU->enablePowerOutput(XPOWERS_ALDO3);

    // ALDO4: GPS
    PMU->setPowerChannelVoltage(XPOWERS_ALDO4, 3300);
    PMU->enablePowerOutput(XPOWERS_ALDO4);

    // BLDO1/BLDO2: SD card
    PMU->setPowerChannelVoltage(XPOWERS_BLDO1, 3300);
    PMU->enablePowerOutput(XPOWERS_BLDO1);
    PMU->setPowerChannelVoltage(XPOWERS_BLDO2, 3300);
    PMU->enablePowerOutput(XPOWERS_BLDO2);

    // DCDC3/4/5: M.2 interface
    PMU->setPowerChannelVoltage(XPOWERS_DCDC3, 3300);
    PMU->enablePowerOutput(XPOWERS_DCDC3);
    PMU->setPowerChannelVoltage(XPOWERS_DCDC4, XPOWERS_AXP2101_DCDC4_VOL2_MAX);
    PMU->enablePowerOutput(XPOWERS_DCDC4);
    PMU->setPowerChannelVoltage(XPOWERS_DCDC5, 3300);
    PMU->enablePowerOutput(XPOWERS_DCDC5);

    // Disable unused channels
    PMU->disablePowerOutput(XPOWERS_DCDC2);
    PMU->disablePowerOutput(XPOWERS_DLDO1);
    PMU->disablePowerOutput(XPOWERS_DLDO2);
    PMU->disablePowerOutput(XPOWERS_VBACKUP);

    Serial.println("PMU initialized - all power rails enabled");
    return true;
}

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
BLEInterface* ble_iface = nullptr;  // Keep pointer for identity setup and Display peer count
AutoInterface* auto_iface = nullptr;  // Keep pointer for Display peer count
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

    Serial.println("=== microReticulum Transport Node ===");
    Serial.println("T-Beam Supreme Edition");

    // Initialize PMU FIRST - powers all peripherals
    if (!pmu_init()) {
        Serial.println("FATAL: PMU init failed - peripherals have no power!");
        // Continue anyway - maybe USB power is enough for testing
    }

    // Small delay for power rails to stabilize
    delay(100);

    RNS::loglevel(RNS::LOG_DEBUG);
    RNS::log("Reticulum logging enabled");

    // Initialize display (after PMU powers it)
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

    // Try to connect WiFi (not required - BLE can work without it)
    bool wifi_ok = wifi_connect();
    if (!wifi_ok) {
        RNS::error("WiFi connection failed - continuing with BLE only");
    }

    // Initialize AutoInterface only if WiFi is connected
    if (wifi_ok) {
        auto_iface = new AutoInterface("Auto");
        auto_interface = auto_iface;
        auto_interface.mode(RNS::Type::Interface::MODE_FULL);
        RNS::Transport::register_interface(auto_interface);
        auto_interface.start();
        RNS::log("AutoInterface started (MODE_FULL)");
    } else {
        RNS::log("Skipping AutoInterface (no WiFi)");
    }

    // Create node identity early (needed for BLE handshake)
    node_identity = RNS::Identity();

    // Initialize BLEInterface (BLE mesh networking)
    ble_iface = new BLEInterface("BLE");
    ble_iface->setDeviceName("TBS-Node");
    ble_iface->setRole(RNS::BLE::Role::DUAL);
    // Set BLE local identity BEFORE start (required for handshake)
    RNS::Bytes ble_id = node_identity.hash();
    ble_id.resize(16);
    ble_iface->setLocalIdentity(ble_id);
    ble_interface = ble_iface;
    ble_interface.mode(RNS::Type::Interface::MODE_FULL);
    RNS::Transport::register_interface(ble_interface);
    ble_interface.start();
    RNS::log("BLEInterface started (MODE_FULL, dual-mode)");

    // Start Reticulum
    reticulum.start();
    RNS::log("Reticulum started");

    // Create node destination
    node_destination = RNS::Destination(
        node_identity,
        RNS::Type::Destination::IN,
        RNS::Type::Destination::SINGLE,
        APP_NAME,
        "node"
    );
    node_destination.set_link_established_callback(on_link_established);
    node_destination.set_proof_strategy(RNS::Type::Destination::PROVE_ALL);

    RNS::log("Transport Node ready");
    RNS::log("  Destination: " + node_destination.hash().toHex());
    RNS::log("  Identity:    " + node_identity.hash().toHex().substr(0, 12) + "...");

    // Configure display with data sources
    #ifdef HAS_DISPLAY
    RNS::Display::set_identity(node_identity);
    // Register all interfaces for display
    if (auto_iface) {
        RNS::Display::add_interface(&auto_interface);
    }
    RNS::Display::add_interface(&ble_interface);
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

    // Update display with peer counts
    #ifdef HAS_DISPLAY
    if (ble_iface) {
        RNS::Display::set_ble_peers(ble_iface->peerCount());
    }
    if (auto_iface) {
        RNS::Display::set_auto_peers(auto_iface->peer_count());
    }
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

#endif // MINIMAL_SERIAL_TEST
