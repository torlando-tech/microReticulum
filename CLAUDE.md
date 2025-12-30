never use stty directly. it will cause claude code to freeze

# T-Beam Supreme (lilygo_tbeam_supreme) Build Configuration

## Critical: build_src_filter Required

The `lilygo_tbeam_supreme` environment MUST use `build_src_filter` to compile the example instead of the full library in `src/`:

```ini
build_src_filter = -<*> +<../examples/transport_node_tbeam_supreme/main.cpp>
```

Without this, PlatformIO compiles `src/` (the full microReticulum library) instead of the example, resulting in firmware that doesn't match what you're editing.

## XPowersLib API

Use `XPowersLibInterface *PMU` (not `XPowersAXP2101 *`) - the interface class has public methods for `setPowerChannelVoltage()`, `enablePowerOutput()`, etc. The concrete class has these as protected.

```cpp
XPowersLibInterface *PMU = nullptr;
XPowersAXP2101 *pmu = new XPowersAXP2101(Wire1);
if (pmu->init()) {
    PMU = pmu;  // Use interface for subsequent calls
    PMU->setPowerChannelVoltage(XPOWERS_ALDO1, 3300);
    PMU->enablePowerOutput(XPOWERS_ALDO1);
}
```

## T-Beam Supreme Pin Configuration

- Display I2C: SDA=17, SCL=18 (Wire)
- PMU I2C: SDA=42, SCL=41 (Wire1)
- PMU uses AXP2101 chip
- Display is SH1106 at address 0x3C

## PMU Initialization Order (Critical)

1. Initialize Wire1 for PMU I2C first
2. Initialize PMU and enable power rails
3. Power cycle on cold boot (disable ALDO1/ALDO2/BLDO1, delay 250ms, then enable)
4. Wait 200ms for power to stabilize
5. Initialize Wire for display I2C
6. Initialize display

## Power Rails for T-Beam Supreme

- ALDO1: Sensors (IMU, magnetometer, BME280) and OLED display
- ALDO2: Sensor communication (I2C)
- ALDO3: LoRa radio
- ALDO4: GPS
- BLDO1/BLDO2: SD card
- DCDC3/4/5: M.2 interface

## BLE Notes

- BLE MAC is WiFi MAC + 1 (e.g., WiFi 48:CA:43:5A:7D:C4 -> BLE 48:CA:43:5A:7D:C5)
- NimBLE-Arduino 2.x works with ESP32-S3

### NimBLE Advertising Fix (Critical for ESP32-S3)

**Problem**: NimBLE's `pAdv->start()` returns true and `isAdvertising()` returns true, but the device is NOT visible in BLE scans.

**Solution**: Call `pAdv->reset()` BEFORE configuring advertising data:

```cpp
NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();
pAdv->reset();  // CRITICAL - clears stale advertising state
pAdv->addServiceUUID(NimBLEUUID(SERVICE_UUID));
pAdv->setName("DeviceName");
pAdv->start();
```

Without `reset()`, the advertising data may not be properly applied on ESP32-S3. This was discovered after hours of debugging where the device claimed to be advertising but was invisible to all BLE scanners.

### Reticulum BLE Service UUID

The service UUID for RNS BLE is: `37145b00-442d-4a94-917f-8f42c5da28e3`

This UUID is shared between:
- microReticulum (ESP32)
- ble-reticulum (Python/Raspberry Pi)
- Columba (Android RNS client)

## Full Transport Node Build

Build filter for full firmware (library + example + interfaces):
```ini
build_src_filter = +<*> -<main.cpp> +<../examples/transport_node_tbeam_supreme/> +<../examples/common/> -<../examples/common/lora_interface/> -<../.pio/> -<../examples/*/.pio/>
```

Include paths needed:
```ini
-Iexamples/common/tcp_interface
-Iexamples/common/udp_interface
-Iexamples/common/auto_interface
-Iexamples/common/ble_interface
-Iexamples/common/universal_filesystem
```

## USB CDC Serial (ESP32-S3)

USB CDC serial output does NOT work reliably on ESP32-S3 T-Beam Supreme, even though bootloader output works. Use the OLED display for status output instead. This appears to be a known issue with ESP32-S3 and Arduino framework.
