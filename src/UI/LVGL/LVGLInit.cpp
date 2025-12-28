// Copyright (c) 2024 microReticulum contributors
// SPDX-License-Identifier: MIT

#include "LVGLInit.h"

#ifdef ARDUINO

#include "../../Log.h"
#include "../../Hardware/TDeck/Display.h"
#include "../../Hardware/TDeck/Keyboard.h"
#include "../../Hardware/TDeck/Touch.h"
#include "../../Hardware/TDeck/Trackball.h"

using namespace RNS;
using namespace Hardware::TDeck;

namespace UI {
namespace LVGL {

bool LVGLInit::_initialized = false;
lv_disp_t* LVGLInit::_display = nullptr;
lv_indev_t* LVGLInit::_keyboard = nullptr;
lv_indev_t* LVGLInit::_touch = nullptr;
lv_indev_t* LVGLInit::_trackball = nullptr;
lv_group_t* LVGLInit::_default_group = nullptr;

bool LVGLInit::init() {
    if (_initialized) {
        return true;
    }

    INFO("Initializing LVGL");

    // Initialize LVGL library
    lv_init();

    // LVGL 8.x logging is configured via lv_conf.h LV_USE_LOG
    // No runtime callback registration needed

    // Initialize display (this also sets up LVGL display driver)
    if (!Display::init()) {
        ERROR("Failed to initialize display for LVGL");
        return false;
    }
    _display = lv_disp_get_default();

    INFO("  Display initialized");

    // Create default input group for keyboard navigation
    _default_group = lv_group_create();
    if (!_default_group) {
        ERROR("Failed to create input group");
        return false;
    }
    lv_group_set_default(_default_group);

    // Initialize keyboard input
    // TEMPORARILY DISABLED - keyboard causing crashes, needs debugging
    // TODO: Fix I2C bus contention between keyboard and touch
    #if 0
    if (Keyboard::init()) {
        _keyboard = Keyboard::get_indev();
        // Associate keyboard with input group
        if (_keyboard) {
            lv_indev_set_group(_keyboard, _default_group);
            INFO("  Keyboard registered with input group");
        }
    } else {
        WARNING("  Keyboard initialization failed");
    }
    #else
    WARNING("  Keyboard disabled for debugging");
    #endif

    // Initialize touch input
    if (Touch::init()) {
        _touch = lv_indev_get_next(_keyboard);
        INFO("  Touch registered");
    } else {
        WARNING("  Touch initialization failed");
    }

    // Initialize trackball input
    if (Trackball::init()) {
        _trackball = lv_indev_get_next(_touch);
        INFO("  Trackball registered");
    } else {
        WARNING("  Trackball initialization failed");
    }

    // Set default dark theme
    set_theme(true);

    _initialized = true;
    INFO("LVGL initialized successfully");

    return true;
}

bool LVGLInit::init_display_only() {
    if (_initialized) {
        return true;
    }

    INFO("Initializing LVGL (display only)");

    // Initialize LVGL library
    lv_init();

    // LVGL 8.x logging is configured via lv_conf.h LV_USE_LOG
    // No runtime callback registration needed

    // Initialize display
    if (!Display::init()) {
        ERROR("Failed to initialize display for LVGL");
        return false;
    }
    _display = lv_disp_get_default();

    INFO("  Display initialized");

    // Set default dark theme
    set_theme(true);

    _initialized = true;
    INFO("LVGL initialized (display only)");

    return true;
}

void LVGLInit::task_handler() {
    if (!_initialized) {
        return;
    }

    lv_task_handler();
}

uint32_t LVGLInit::get_tick() {
    return millis();
}

bool LVGLInit::is_initialized() {
    return _initialized;
}

void LVGLInit::set_theme(bool dark) {
    if (!_initialized) {
        return;
    }

    lv_theme_t* theme;

    if (dark) {
        // Dark theme with blue accents
        theme = lv_theme_default_init(
            _display,
            lv_palette_main(LV_PALETTE_BLUE),      // Primary color
            lv_palette_main(LV_PALETTE_RED),       // Secondary color
            true,                                    // Dark mode
            &lv_font_montserrat_14                  // Default font
        );
    } else {
        // Light theme
        theme = lv_theme_default_init(
            _display,
            lv_palette_main(LV_PALETTE_BLUE),
            lv_palette_main(LV_PALETTE_RED),
            false,                                   // Light mode
            &lv_font_montserrat_14
        );
    }

    lv_disp_set_theme(_display, theme);
}

lv_disp_t* LVGLInit::get_display() {
    return _display;
}

lv_indev_t* LVGLInit::get_keyboard() {
    return _keyboard;
}

lv_indev_t* LVGLInit::get_touch() {
    return _touch;
}

lv_indev_t* LVGLInit::get_trackball() {
    return _trackball;
}

lv_group_t* LVGLInit::get_default_group() {
    return _default_group;
}

void LVGLInit::focus_widget(lv_obj_t* obj) {
    if (!_default_group || !obj) {
        return;
    }

    // Remove from group first if already there (to avoid duplicates)
    lv_group_remove_obj(obj);

    // Add to group and focus
    lv_group_add_obj(_default_group, obj);
    lv_group_focus_obj(obj);
}

void LVGLInit::log_print(const char* buf) {
    // Forward LVGL logs to our logging system
    // LVGL logs include newlines, so strip them
    String msg(buf);
    msg.trim();

    if (msg.length() > 0) {
        // LVGL log levels: Trace, Info, Warn, Error
        if (msg.indexOf("[Error]") >= 0) {
            ERROR(msg.c_str());
        } else if (msg.indexOf("[Warn]") >= 0) {
            WARNING(msg.c_str());
        } else if (msg.indexOf("[Info]") >= 0) {
            INFO(msg.c_str());
        } else {
            TRACE(msg.c_str());
        }
    }
}

} // namespace LVGL
} // namespace UI

#endif // ARDUINO
