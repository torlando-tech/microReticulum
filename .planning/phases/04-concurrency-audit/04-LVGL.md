# Phase 4: LVGL Thread Safety Audit

**Audit Date:** 2026-01-24
**Status:** Complete
**Requirement:** CONC-01

---

## Executive Summary

The LVGL thread safety audit found **3 Medium severity issues** across the UI subsystem. The core threading model is sound - LVGL runs on a dedicated FreeRTOS task with recursive mutex protection, and the RAII `LVGL_LOCK()` pattern is correctly applied in most locations.

**Issue Summary:**
| Severity | Count | Category |
|----------|-------|----------|
| Critical | 0 | - |
| High | 0 | - |
| Medium | 3 | Missing LVGL_LOCK in constructors/destructors |
| Low | 0 | - |

**Key Finding:** Three screen classes (SettingsScreen, ComposeScreen, AnnounceListScreen) lack `LVGL_LOCK()` in their constructors and destructors. While currently safe due to initialization timing (before LVGL task starts), this creates a latent threading vulnerability if initialization order changes.

---

## Threading Model

### LVGL Task Architecture

```
+------------------+                    +------------------+
|   Main Task      |                    |   LVGL Task      |
|   (Arduino)      |                    |   (Core 1)       |
+------------------+                    +------------------+
|                  |   LVGL_LOCK()      |                  |
| UIManager::      |   ------------->   | lv_task_handler()|
|   init()         |                    |                  |
|   update()       |   <-------------   | Widget render    |
|   show_*()       |    Mutex held      | Event processing |
|                  |                    |                  |
| Callbacks from   |                    | lv_obj_add_      |
|   LXMRouter      |                    |   event_cb()     |
+------------------+                    +------------------+
         |                                      |
         v                                      v
+------------------+                    +------------------+
| LVGL Recursive   |<==================>| Same mutex       |
| Mutex            |                    | (owned by task)  |
| portMAX_DELAY    |                    |                  |
+------------------+                    +------------------+
```

### Mutex Implementation

**Type:** Recursive mutex (`xSemaphoreCreateRecursiveMutex`)

**Creation** (`LVGLInit.cpp:38`):
```cpp
_mutex = xSemaphoreCreateRecursiveMutex();
if (!_mutex) {
    ERROR("Failed to create LVGL mutex");
    return false;
}
```

**LVGL Task Loop** (`LVGLInit.cpp:155-165`):
```cpp
while (true) {
    if (xSemaphoreTakeRecursive(_mutex, portMAX_DELAY) == pdTRUE) {
        lv_task_handler();
        xSemaphoreGiveRecursive(_mutex);
    }
    vTaskDelay(pdMS_TO_TICKS(5));  // 5ms between iterations
}
```

**RAII Lock Pattern** (`LVGLLock.h:32-58`):
```cpp
class LVGLLock {
public:
    LVGLLock() {
        SemaphoreHandle_t mutex = LVGLInit::get_mutex();
        if (mutex) {
            xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
            _acquired = true;
        }
    }
    ~LVGLLock() {
        if (_acquired) {
            SemaphoreHandle_t mutex = LVGLInit::get_mutex();
            if (mutex) {
                xSemaphoreGiveRecursive(mutex);
            }
        }
    }
private:
    bool _acquired = false;
};

#define LVGL_LOCK() UI::LVGL::LVGLLock _lvgl_lock_guard
```

**Recursive mutex rationale:** LVGL event callbacks may call other LVGL functions, and the same thread may need to re-acquire the mutex. Recursive mutex allows this without deadlock.

---

## Mutex Protection Audit

### Coverage Summary

| File | lv_* Calls | LVGL_LOCK() | Coverage | Issues |
|------|------------|-------------|----------|--------|
| UIManager.cpp | ~80 | 15 | Full | None |
| StatusScreen.cpp | ~120 | 8 | Full | None |
| SettingsScreen.cpp | ~300 | 6 | **Partial** | Constructor/Destructor |
| ChatScreen.cpp | ~100 | 9 | Full | None |
| ConversationListScreen.cpp | ~100 | 8 | Full | None |
| ComposeScreen.cpp | ~50 | 4 | **Partial** | Constructor/Destructor |
| AnnounceListScreen.cpp | ~60 | 3 | **Partial** | Constructor/Destructor |
| PropagationNodesScreen.cpp | ~80 | 4 | Full | None |
| QRScreen.cpp | ~30 | 5 | Full | None |

**Total:** 64 LVGL_LOCK() calls across 10 files

### Protected Methods by File

**UIManager.cpp (15 locks):**
- `init()` - Screen creation and callback setup
- `update()` - Periodic UI refresh
- `show_conversation_list()`, `show_chat()`, `show_compose()`, `show_announces()`, `show_status()`, `show_settings()`, `show_propagation_nodes()` - Screen transitions
- `on_share_from_status()`, `on_back_from_qr()` - QR screen transitions
- `on_message_received()`, `on_message_delivered()`, `on_message_failed()` - LXMF callbacks
- `refresh_current_screen()` - Refresh operations

**StatusScreen.cpp (8 locks):**
- Destructor, `set_identity_hash()`, `set_lxmf_address()`, `set_rns_status()`, `set_ble_info()`, `refresh()`, `show()`, `hide()`

**ChatScreen.cpp (9 locks):**
- Constructor, Destructor, `load_conversation()`, `refresh()`, `load_more_messages()`, `add_message()`, `update_message_status()`, `show()`, `hide()`

**ConversationListScreen.cpp (8 locks):**
- Constructor, Destructor, `load_conversations()`, `refresh()`, `update_unread_count()`, `show()`, `hide()`, `update_status()`

**PropagationNodesScreen.cpp (4 locks):**
- Destructor, `load_nodes()`, `show()`, `hide()`

**QRScreen.cpp (5 locks):**
- Destructor, `set_identity()`, `set_lxmf_address()`, `show()`, `hide()`

---

## Event Handler Analysis

All LVGL event handlers are registered via `lv_obj_add_event_cb()` and run within LVGL task context during `lv_task_handler()`. Since the LVGL task holds the recursive mutex while processing events, these handlers can safely call LVGL APIs without explicit locking.

### Event Handlers by File

**StatusScreen.cpp:**
| Handler | Event | Threading Context | Safe |
|---------|-------|-------------------|------|
| `on_back_clicked` | LV_EVENT_CLICKED | LVGL task | Yes |
| `on_share_clicked` | LV_EVENT_CLICKED | LVGL task | Yes |

**SettingsScreen.cpp:**
| Handler | Event | Threading Context | Safe |
|---------|-------|-------------------|------|
| `on_back_clicked` | LV_EVENT_CLICKED | LVGL task | Yes |
| `on_save_clicked` | LV_EVENT_CLICKED | LVGL task | Yes |
| `on_reconnect_clicked` | LV_EVENT_CLICKED | LVGL task | Yes |
| `on_brightness_changed` | LV_EVENT_VALUE_CHANGED | LVGL task | Yes |
| `on_lora_enabled_changed` | LV_EVENT_VALUE_CHANGED | LVGL task | Yes |
| `on_lora_power_changed` | LV_EVENT_VALUE_CHANGED | LVGL task | Yes |
| `on_notification_volume_changed` | LV_EVENT_VALUE_CHANGED | LVGL task | Yes |
| `on_propagation_nodes_clicked` | LV_EVENT_CLICKED | LVGL task | Yes |

**ChatScreen.cpp:**
| Handler | Event | Threading Context | Safe |
|---------|-------|-------------------|------|
| `on_back_clicked` | LV_EVENT_CLICKED | LVGL task | Yes |
| `on_send_clicked` | LV_EVENT_CLICKED | LVGL task | Yes |
| `on_scroll` | LV_EVENT_SCROLL_END | LVGL task | Yes |
| `on_textarea_long_pressed` | LV_EVENT_LONG_PRESSED | LVGL task | Yes |
| `on_message_long_pressed` | LV_EVENT_LONG_PRESSED | LVGL task | Yes |
| `on_copy_dialog_action` | LV_EVENT_VALUE_CHANGED | LVGL task | Yes |
| `on_paste_dialog_action` | LV_EVENT_VALUE_CHANGED | LVGL task | Yes |

**ConversationListScreen.cpp:**
| Handler | Event | Threading Context | Safe |
|---------|-------|-------------------|------|
| `on_conversation_clicked` | LV_EVENT_CLICKED | LVGL task | Yes |
| `on_conversation_long_pressed` | LV_EVENT_LONG_PRESSED | LVGL task | Yes |
| `on_sync_clicked` | LV_EVENT_CLICKED | LVGL task | Yes |
| `on_bottom_nav_clicked` | LV_EVENT_CLICKED | LVGL task | Yes |
| `on_delete_confirmed` | LV_EVENT_VALUE_CHANGED | LVGL task | Yes |

**ComposeScreen.cpp:**
| Handler | Event | Threading Context | Safe |
|---------|-------|-------------------|------|
| `on_back_clicked` | LV_EVENT_CLICKED | LVGL task | Yes |
| `on_cancel_clicked` | LV_EVENT_CLICKED | LVGL task | Yes |
| `on_send_clicked` | LV_EVENT_CLICKED | LVGL task | Yes |

**AnnounceListScreen.cpp:**
| Handler | Event | Threading Context | Safe |
|---------|-------|-------------------|------|
| `on_announce_clicked` | LV_EVENT_CLICKED | LVGL task | Yes |
| `on_back_clicked` | LV_EVENT_CLICKED | LVGL task | Yes |
| `on_refresh_clicked` | LV_EVENT_CLICKED | LVGL task | Yes |
| `on_send_announce_clicked` | LV_EVENT_CLICKED | LVGL task | Yes |

**PropagationNodesScreen.cpp:**
| Handler | Event | Threading Context | Safe |
|---------|-------|-------------------|------|
| `on_node_clicked` | LV_EVENT_CLICKED | LVGL task | Yes |
| `on_back_clicked` | LV_EVENT_CLICKED | LVGL task | Yes |
| `on_sync_clicked` | LV_EVENT_CLICKED | LVGL task | Yes |
| `on_auto_select_changed` | LV_EVENT_CLICKED | LVGL task | Yes |

**QRScreen.cpp:**
| Handler | Event | Threading Context | Safe |
|---------|-------|-------------------|------|
| `on_back_clicked` | LV_EVENT_CLICKED | LVGL task | Yes |

**TextAreaHelper.h:**
| Handler | Event | Threading Context | Safe |
|---------|-------|-------------------|------|
| `on_long_pressed` | LV_EVENT_LONG_PRESSED | LVGL task | Yes |
| `on_paste_action` | LV_EVENT_VALUE_CHANGED | LVGL task | Yes |

**Total:** 36 event callback registrations, all running in LVGL task context (thread-safe).

---

## Findings

### Finding 1: SettingsScreen Constructor Missing LVGL_LOCK

**Severity:** Medium
**Location:** `src/UI/LXMF/SettingsScreen.cpp:55-103`

**Description:**
The SettingsScreen constructor creates approximately 100+ LVGL widgets without acquiring the LVGL mutex. While this is currently safe because screens are created during `UIManager::init()` which holds `LVGL_LOCK()`, the constructor itself is not inherently thread-safe.

**Code Snippet:**
```cpp
SettingsScreen::SettingsScreen(lv_obj_t* parent)
    : _screen(nullptr), ... {
    // NO LVGL_LOCK() here
    if (parent) {
        _screen = lv_obj_create(parent);  // Unprotected LVGL call
    } else {
        _screen = lv_obj_create(lv_scr_act());
    }
    lv_obj_set_size(_screen, LV_PCT(100), LV_PCT(100));
    // ... 100+ more unprotected lv_* calls
}
```

**Risk Assessment:**
- Currently mitigated by UIManager holding lock during screen creation
- If constructor called outside UIManager context, race condition possible
- Could cause widget corruption or crash if LVGL task is running

**Recommended Fix:**
```cpp
SettingsScreen::SettingsScreen(lv_obj_t* parent) : ... {
    LVGL_LOCK();  // Add this line
    // ... rest of constructor
}
```

### Finding 2: SettingsScreen Destructor Missing LVGL_LOCK

**Severity:** Medium
**Location:** `src/UI/LXMF/SettingsScreen.cpp:105-109`

**Description:**
The destructor calls `lv_obj_del()` without mutex protection.

**Code Snippet:**
```cpp
SettingsScreen::~SettingsScreen() {
    // NO LVGL_LOCK() here
    if (_screen) {
        lv_obj_del(_screen);  // Unprotected LVGL call
    }
}
```

**Risk Assessment:**
- `lv_obj_del()` modifies internal LVGL structures
- If called while LVGL task is rendering, corruption possible
- Screens are deleted in UIManager destructor which does not hold lock

**Recommended Fix:**
```cpp
SettingsScreen::~SettingsScreen() {
    LVGL_LOCK();  // Add this line
    if (_screen) {
        lv_obj_del(_screen);
    }
}
```

### Finding 3: ComposeScreen and AnnounceListScreen Same Pattern

**Severity:** Medium
**Location:**
- `src/UI/LXMF/ComposeScreen.cpp:19-54` (constructor)
- `src/UI/LXMF/ComposeScreen.cpp:50-54` (destructor)
- `src/UI/LXMF/AnnounceListScreen.cpp:23-50` (constructor)
- `src/UI/LXMF/AnnounceListScreen.cpp:52-56` (destructor)

**Description:**
Same pattern as SettingsScreen - constructors and destructors call LVGL APIs without mutex protection.

**Code Snippet (ComposeScreen):**
```cpp
ComposeScreen::ComposeScreen(lv_obj_t* parent) : ... {
    // NO LVGL_LOCK()
    _screen = lv_obj_create(parent ? parent : lv_scr_act());
    // ... widget creation
}

ComposeScreen::~ComposeScreen() {
    // NO LVGL_LOCK()
    if (_screen) {
        lv_obj_del(_screen);
    }
}
```

**Code Snippet (AnnounceListScreen):**
```cpp
AnnounceListScreen::AnnounceListScreen(lv_obj_t* parent) : ... {
    // NO LVGL_LOCK()
    _screen = lv_obj_create(parent ? parent : lv_scr_act());
    // ... widget creation
}

AnnounceListScreen::~AnnounceListScreen() {
    // NO LVGL_LOCK()
    if (_screen) {
        lv_obj_del(_screen);
    }
}
```

**Recommended Fix:** Add `LVGL_LOCK()` to both constructors and destructors.

---

## Positive Patterns

### 1. RAII Lock Pattern
The `LVGL_LOCK()` macro using RAII ensures locks are automatically released when scope ends, preventing forgotten unlocks.

### 2. Recursive Mutex
Recursive mutex correctly handles nested LVGL calls from event callbacks.

### 3. UIManager Centralization
All screen transitions go through UIManager with explicit locking, providing a single point of thread-safety enforcement.

### 4. Proper Event Handler Context
All 36 event callbacks correctly run in LVGL task context where mutex is already held.

### 5. Well-Protected Screens
ChatScreen, StatusScreen, ConversationListScreen, QRScreen, and PropagationNodesScreen all have `LVGL_LOCK()` in constructors and destructors.

---

## ASCII Diagram: LVGL Task and Mutex Relationship

```
                           ESP32-S3 Dual-Core
                    +---------------------------+
                    |         Core 0            |         Core 1
                    | +----------------------+  | +----------------------+
                    | |     Main Task        |  | |     LVGL Task        |
                    | |  (Arduino loop())    |  | |  (lvgl_task())       |
                    | +----------+-----------+  | +----------+-----------+
                    |            |              |            |
                    |            v              |            v
                    | +----------+-----------+  | +----------+-----------+
                    | | LVGL_LOCK()          |  | | Mutex held during    |
                    | |   UIManager::        |  | |   lv_task_handler()  |
                    | |     update()         |  | +----------+-----------+
                    | |     show_*()         |  |            |
                    | +----------+-----------+  |            v
                    |            |              | +----------+-----------+
                    |            |              | | Event Callbacks      |
                    |            |              | |  (already protected) |
                    |            |              | +----------------------+
                    +------------|--------------+
                                 |
                                 v
                    +---------------------------+
                    |   Recursive Mutex         |
                    |   (xSemaphoreCreateRecursiveMutex)
                    |                           |
                    |   portMAX_DELAY wait      |
                    |   Same-thread reentry OK  |
                    +---------------------------+
                                 ^
                                 |
              +------------------+------------------+
              |                                     |
  +-----------+-----------+           +-------------+-------------+
  | Screen Constructors   |           | LXMF Callbacks            |
  | (some missing lock)   |           | (UIManager with lock)     |
  +-----------------------+           +---------------------------+
```

---

## Cross-Reference

This audit addresses **CONC-01: Audit LVGL Mutex Usage for Thread Safety** from the Phase 4 requirements.

Related documents:
- Threading architecture: `.planning/phases/04-concurrency-audit/04-RESEARCH.md`
- Memory pools: `.planning/phases/03-memory-allocation-audit/03-AUDIT.md` (screen objects documented)

---

## Recommendations

1. **Add LVGL_LOCK() to unprotected constructors/destructors** (Medium priority)
   - SettingsScreen, ComposeScreen, AnnounceListScreen
   - Low risk since UIManager currently protects, but defensive coding

2. **Document threading invariants** (Low priority)
   - Add comment to UIManager::init() noting it provides lock for screen construction
   - Add comment to screen base classes about expected lock state

3. **Consider lock timeout monitoring** (Low priority for Phase 5)
   - portMAX_DELAY could hide deadlocks
   - Add debug logging if lock held > 100ms

---

*Phase: 04-concurrency-audit*
*Document: 04-LVGL.md*
*Generated: 2026-01-24*
