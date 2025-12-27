// Copyright (c) 2024 microReticulum contributors
// SPDX-License-Identifier: MIT

#include "Trackball.h"

#ifdef ARDUINO

#include "../../Log.h"

using namespace RNS;

namespace Hardware {
namespace TDeck {

// Static member initialization
volatile int16_t Trackball::_pulse_up = 0;
volatile int16_t Trackball::_pulse_down = 0;
volatile int16_t Trackball::_pulse_left = 0;
volatile int16_t Trackball::_pulse_right = 0;
volatile uint32_t Trackball::_last_pulse_time = 0;

bool Trackball::_button_pressed = false;
uint32_t Trackball::_last_button_time = 0;
Trackball::State Trackball::_state;
bool Trackball::_initialized = false;

bool Trackball::init() {
    if (_initialized) {
        return true;
    }

    INFO("Initializing T-Deck trackball");

    // Initialize hardware first
    if (!init_hardware_only()) {
        return false;
    }

    // Register LVGL input device
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_ENCODER;
    indev_drv.read_cb = lvgl_read_cb;

    lv_indev_t* indev = lv_indev_drv_register(&indev_drv);
    if (!indev) {
        ERROR("Failed to register trackball with LVGL");
        return false;
    }

    INFO("Trackball initialized successfully");
    return true;
}

bool Trackball::init_hardware_only() {
    if (_initialized) {
        return true;
    }

    INFO("Initializing trackball hardware");

    // Configure directional pins as inputs with pullup
    pinMode(Pin::TRACKBALL_UP, INPUT_PULLUP);
    pinMode(Pin::TRACKBALL_DOWN, INPUT_PULLUP);
    pinMode(Pin::TRACKBALL_LEFT, INPUT_PULLUP);
    pinMode(Pin::TRACKBALL_RIGHT, INPUT_PULLUP);
    pinMode(Pin::TRACKBALL_BUTTON, INPUT_PULLUP);

    // Attach interrupts for directional pins (FALLING edge = pulse detected)
    attachInterrupt(digitalPinToInterrupt(Pin::TRACKBALL_UP), isr_up, FALLING);
    attachInterrupt(digitalPinToInterrupt(Pin::TRACKBALL_DOWN), isr_down, FALLING);
    attachInterrupt(digitalPinToInterrupt(Pin::TRACKBALL_LEFT), isr_left, FALLING);
    attachInterrupt(digitalPinToInterrupt(Pin::TRACKBALL_RIGHT), isr_right, FALLING);

    // Initialize state
    _state.delta_x = 0;
    _state.delta_y = 0;
    _state.button_pressed = false;
    _state.timestamp = millis();

    _initialized = true;
    INFO("  Trackball hardware ready");
    return true;
}

bool Trackball::poll() {
    if (!_initialized) {
        return false;
    }

    bool state_changed = false;
    uint32_t now = millis();

    // Read pulse counters (critical section to avoid race with ISRs)
    noInterrupts();
    int16_t up = _pulse_up;
    int16_t down = _pulse_down;
    int16_t left = _pulse_left;
    int16_t right = _pulse_right;
    uint32_t last_pulse = _last_pulse_time;
    interrupts();

    // Calculate net movement
    int16_t delta_y = down - up;  // Positive = down, negative = up
    int16_t delta_x = right - left;  // Positive = right, negative = left

    // Apply sensitivity multiplier
    delta_x *= Trk::PIXELS_PER_PULSE;
    delta_y *= Trk::PIXELS_PER_PULSE;

    // Update state if movement detected
    if (delta_x != 0 || delta_y != 0) {
        _state.delta_x = delta_x;
        _state.delta_y = delta_y;
        _state.timestamp = now;
        state_changed = true;

        // Reset pulse counters after reading
        noInterrupts();
        _pulse_up = 0;
        _pulse_down = 0;
        _pulse_left = 0;
        _pulse_right = 0;
        interrupts();
    } else {
        // Reset deltas if no recent pulses (timeout)
        if (now - last_pulse > Trk::PULSE_RESET_MS) {
            if (_state.delta_x != 0 || _state.delta_y != 0) {
                _state.delta_x = 0;
                _state.delta_y = 0;
                state_changed = true;
            }
        }
    }

    // Read button state with debouncing
    bool button = read_button_debounced();
    if (button != _state.button_pressed) {
        _state.button_pressed = button;
        state_changed = true;
    }

    return state_changed;
}

void Trackball::get_state(State& state) {
    state = _state;
}

void Trackball::reset_deltas() {
    _state.delta_x = 0;
    _state.delta_y = 0;
}

bool Trackball::is_button_pressed() {
    return _state.button_pressed;
}

void Trackball::lvgl_read_cb(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    // Poll for new trackball data
    poll();

    // Get current state
    State state;
    get_state(state);

    // For encoder mode, we use delta_y for vertical scrolling
    // and button for enter/select
    data->enc_diff = -state.delta_y / Trk::PIXELS_PER_PULSE;  // Invert Y for natural scrolling

    if (state.button_pressed) {
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }

    // Reset deltas after reading for LVGL
    reset_deltas();
}

bool Trackball::read_button_debounced() {
    bool current = (digitalRead(Pin::TRACKBALL_BUTTON) == LOW);  // Active low
    uint32_t now = millis();

    // Debounce: only accept change if stable for debounce period
    if (current != _button_pressed) {
        if (now - _last_button_time > Trk::DEBOUNCE_MS) {
            _last_button_time = now;
            return current;
        }
    } else {
        _last_button_time = now;
    }

    return _button_pressed;
}

// ISR handlers - MUST be in IRAM for ESP32
void IRAM_ATTR Trackball::isr_up() {
    _pulse_up++;
    _last_pulse_time = millis();
}

void IRAM_ATTR Trackball::isr_down() {
    _pulse_down++;
    _last_pulse_time = millis();
}

void IRAM_ATTR Trackball::isr_left() {
    _pulse_left++;
    _last_pulse_time = millis();
}

void IRAM_ATTR Trackball::isr_right() {
    _pulse_right++;
    _last_pulse_time = millis();
}

} // namespace TDeck
} // namespace Hardware

#endif // ARDUINO
