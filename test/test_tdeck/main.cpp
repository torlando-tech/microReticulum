// T-Deck Hardware Test Suite
// Tests all T-Deck hardware drivers without LVGL

#include <Arduino.h>
#include <Wire.h>
#include "../../src/Hardware/TDeck/Config.h"
#include "../../src/Hardware/TDeck/Display.h"
#include "../../src/Hardware/TDeck/Keyboard.h"
#include "../../src/Hardware/TDeck/Touch.h"
#include "../../src/Hardware/TDeck/Trackball.h"

using namespace Hardware::TDeck;

// Test modes
enum TestMode {
    TEST_DISPLAY,
    TEST_KEYBOARD,
    TEST_TOUCH,
    TEST_TRACKBALL,
    TEST_ALL
};

TestMode currentTest = TEST_DISPLAY;
uint32_t lastUpdate = 0;
uint32_t testStartTime = 0;

// Display test colors (RGB565)
const uint16_t COLOR_RED = 0xF800;
const uint16_t COLOR_GREEN = 0x07E0;
const uint16_t COLOR_BLUE = 0x001F;
const uint16_t COLOR_WHITE = 0xFFFF;
const uint16_t COLOR_BLACK = 0x0000;
const uint16_t COLOR_YELLOW = 0xFFE0;
const uint16_t COLOR_CYAN = 0x07FF;
const uint16_t COLOR_MAGENTA = 0xF81F;

void testDisplay() {
    Serial.println("\n=== DISPLAY TEST ===");
    Serial.println("Testing ST7789V display driver");

    // Test 1: Fill screen with solid colors
    Serial.println("Test 1: Solid colors (2s each)");
    Display::fill_screen(COLOR_RED);
    Serial.println("  RED");
    delay(2000);

    Display::fill_screen(COLOR_GREEN);
    Serial.println("  GREEN");
    delay(2000);

    Display::fill_screen(COLOR_BLUE);
    Serial.println("  BLUE");
    delay(2000);

    // Test 2: Color bars
    Serial.println("Test 2: Color bars");
    Display::fill_screen(COLOR_BLACK);

    uint16_t bar_height = Display::HEIGHT / 8;
    Display::draw_rect(0, bar_height * 0, Display::WIDTH, bar_height, COLOR_RED);
    Display::draw_rect(0, bar_height * 1, Display::WIDTH, bar_height, COLOR_GREEN);
    Display::draw_rect(0, bar_height * 2, Display::WIDTH, bar_height, COLOR_BLUE);
    Display::draw_rect(0, bar_height * 3, Display::WIDTH, bar_height, COLOR_YELLOW);
    Display::draw_rect(0, bar_height * 4, Display::WIDTH, bar_height, COLOR_CYAN);
    Display::draw_rect(0, bar_height * 5, Display::WIDTH, bar_height, COLOR_MAGENTA);
    Display::draw_rect(0, bar_height * 6, Display::WIDTH, bar_height, COLOR_WHITE);
    Display::draw_rect(0, bar_height * 7, Display::WIDTH, bar_height, COLOR_BLACK);

    delay(3000);

    // Test 3: Brightness control
    Serial.println("Test 3: Brightness fade");
    Display::fill_screen(COLOR_WHITE);

    for (int i = 255; i >= 0; i -= 5) {
        Display::set_brightness(i);
        delay(20);
    }

    for (int i = 0; i <= 255; i += 5) {
        Display::set_brightness(i);
        delay(20);
    }

    Serial.println("Display test complete!");
    Display::fill_screen(COLOR_BLACK);
}

void testKeyboard() {
    Serial.println("\n=== KEYBOARD TEST ===");
    Serial.println("Testing ESP32-C3 keyboard controller");
    Serial.println("Press keys on the keyboard (ESC to exit)");

    Display::fill_screen(COLOR_BLACK);
    Display::draw_rect(10, 10, 300, 50, COLOR_BLUE);

    testStartTime = millis();

    while (millis() - testStartTime < 30000) {  // 30 second timeout
        Keyboard::poll();

        if (Keyboard::available()) {
            char key = Keyboard::read_key();

            if (key == 0x1B) {  // ESC key
                Serial.println("ESC pressed - exiting keyboard test");
                break;
            }

            Serial.print("Key pressed: 0x");
            Serial.print(key, HEX);
            Serial.print(" (");
            if (key >= 32 && key <= 126) {
                Serial.print((char)key);
            } else {
                Serial.print("special");
            }
            Serial.println(")");

            // Visual feedback
            static int rect_x = 20;
            Display::draw_rect(rect_x, 20, 30, 30, COLOR_GREEN);
            rect_x += 35;
            if (rect_x > 270) rect_x = 20;
        }

        delay(10);
    }

    Serial.println("Keyboard test complete!");
    Display::fill_screen(COLOR_BLACK);
}

void testTouch() {
    Serial.println("\n=== TOUCH TEST ===");
    Serial.println("Testing GT911 touch controller (polling mode)");
    Serial.println("Touch the screen (touch for 3s to exit)");

    String product_id = Touch::get_product_id();
    Serial.print("Product ID: ");
    Serial.println(product_id);

    Display::fill_screen(COLOR_BLACK);
    Display::draw_rect(0, 0, Display::WIDTH, 30, COLOR_BLUE);

    testStartTime = millis();
    uint32_t touch_start = 0;
    bool touching = false;

    while (millis() - testStartTime < 60000) {  // 60 second timeout
        Touch::poll();

        uint8_t touch_count = Touch::get_touch_count();

        if (touch_count > 0) {
            if (!touching) {
                touching = true;
                touch_start = millis();
            }

            // Check for long press to exit
            if (millis() - touch_start > 3000) {
                Serial.println("Long touch detected - exiting touch test");
                break;
            }

            for (uint8_t i = 0; i < touch_count; i++) {
                Touch::TouchPoint point;
                if (Touch::get_point(i, point)) {
                    Serial.print("Touch point ");
                    Serial.print(i);
                    Serial.print(": (");
                    Serial.print(point.x);
                    Serial.print(", ");
                    Serial.print(point.y);
                    Serial.print(") size=");
                    Serial.println(point.size);

                    // Draw circle at touch point
                    Display::draw_rect(point.x - 5, point.y - 5, 10, 10, COLOR_RED);
                }
            }
        } else {
            if (touching) {
                touching = false;
                Serial.println("Touch released");
            }
        }

        delay(10);
    }

    Serial.println("Touch test complete!");
    Display::fill_screen(COLOR_BLACK);
}

void testTrackball() {
    Serial.println("\n=== TRACKBALL TEST ===");
    Serial.println("Testing trackball (GPIO pulse-based)");
    Serial.println("Move trackball and press button (button for 3s to exit)");

    Display::fill_screen(COLOR_BLACK);

    // Draw cursor
    int16_t cursor_x = Display::WIDTH / 2;
    int16_t cursor_y = Display::HEIGHT / 2;
    Display::draw_rect(cursor_x - 5, cursor_y - 5, 10, 10, COLOR_GREEN);

    testStartTime = millis();
    uint32_t button_press_start = 0;
    bool button_was_pressed = false;

    while (millis() - testStartTime < 60000) {  // 60 second timeout
        if (Trackball::poll()) {
            Trackball::State state;
            Trackball::get_state(state);

            // Handle movement
            if (state.delta_x != 0 || state.delta_y != 0) {
                Serial.print("Trackball delta: (");
                Serial.print(state.delta_x);
                Serial.print(", ");
                Serial.print(state.delta_y);
                Serial.println(")");

                // Erase old cursor
                Display::draw_rect(cursor_x - 5, cursor_y - 5, 10, 10, COLOR_BLACK);

                // Update cursor position
                cursor_x += state.delta_x;
                cursor_y += state.delta_y;

                // Clamp to screen
                if (cursor_x < 5) cursor_x = 5;
                if (cursor_x > Display::WIDTH - 6) cursor_x = Display::WIDTH - 6;
                if (cursor_y < 5) cursor_y = 5;
                if (cursor_y > Display::HEIGHT - 6) cursor_y = Display::HEIGHT - 6;

                // Draw new cursor
                Display::draw_rect(cursor_x - 5, cursor_y - 5, 10, 10, COLOR_GREEN);
            }

            // Handle button
            if (state.button_pressed) {
                if (!button_was_pressed) {
                    button_was_pressed = true;
                    button_press_start = millis();
                    Serial.println("Button pressed");

                    // Visual feedback
                    Display::draw_rect(cursor_x - 5, cursor_y - 5, 10, 10, COLOR_RED);
                }

                // Check for long press to exit
                if (millis() - button_press_start > 3000) {
                    Serial.println("Long button press - exiting trackball test");
                    break;
                }
            } else {
                if (button_was_pressed) {
                    button_was_pressed = false;
                    Serial.println("Button released");

                    // Restore cursor color
                    Display::draw_rect(cursor_x - 5, cursor_y - 5, 10, 10, COLOR_GREEN);
                }
            }
        }

        delay(10);
    }

    Serial.println("Trackball test complete!");
    Display::fill_screen(COLOR_BLACK);
}

void setup() {
    Serial.begin(115200);
    delay(2000);  // Wait for serial monitor

    Serial.println("\n\n=================================");
    Serial.println("T-Deck Hardware Test Suite");
    Serial.println("=================================\n");

    // Initialize I2C for keyboard and touch
    Serial.println("Initializing I2C bus...");
    Wire.begin(Pin::I2C_SDA, Pin::I2C_SCL);
    Wire.setClock(I2C::FREQUENCY);
    Serial.println("  I2C ready");

    // Initialize display
    Serial.println("\nInitializing display...");
    if (!Display::init_hardware_only()) {
        Serial.println("ERROR: Display initialization failed!");
        while (1) delay(1000);
    }
    Serial.println("  Display ready");

    // Initialize keyboard
    Serial.println("\nInitializing keyboard...");
    if (!Keyboard::init_hardware_only(Wire)) {
        Serial.println("WARNING: Keyboard initialization failed (may not be critical)");
    } else {
        Serial.println("  Keyboard ready");
    }

    // Initialize touch
    Serial.println("\nInitializing touch...");
    if (!Touch::init_hardware_only(Wire)) {
        Serial.println("WARNING: Touch initialization failed (may not be critical)");
    } else {
        Serial.println("  Touch ready");
    }

    // Initialize trackball
    Serial.println("\nInitializing trackball...");
    if (!Trackball::init_hardware_only()) {
        Serial.println("WARNING: Trackball initialization failed (may not be critical)");
    } else {
        Serial.println("  Trackball ready");
    }

    Serial.println("\n=================================");
    Serial.println("All hardware initialized!");
    Serial.println("=================================\n");

    delay(2000);
}

void loop() {
    // Run tests sequentially
    testDisplay();
    delay(2000);

    testKeyboard();
    delay(2000);

    testTouch();
    delay(2000);

    testTrackball();
    delay(2000);

    Serial.println("\n=================================");
    Serial.println("All tests complete!");
    Serial.println("Restarting in 5 seconds...");
    Serial.println("=================================\n");

    delay(5000);
}
