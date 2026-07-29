#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------------------
//  Multi-boot lifecycle: which of the three firmwares the bootloader runs next.
//
//  Menu -> app:  point the boot register at an OTA slot and reboot.
//  App -> menu:  point it back at the factory partition and reboot.
//
//  The launcher lives in `factory`, so a blank or corrupt otadata lands there
//  too -- you cannot brick yourself into an app with no way out.
// ---------------------------------------------------------------------------
namespace app_switch {

enum class Slot : uint8_t {
    Menu,     // factory  @ 0x010000 -- launcher
    Csi,      // ota_0    @ 0x210000 -- CSI radar tracker
    Scanner,  // ota_1    @ 0x610000 -- WiFi + BLE proximity sniffer
    Toolkit,  // ota_2    @ 0xA10000 -- RF security toolkit
};

// True if a real firmware image has been flashed into that slot. Lets the
// launcher grey out apps you have not built yet instead of rebooting into a
// blank partition and bouncing straight back.
bool isInstalled(Slot slot);

// True when this binary is the launcher itself. The escape hatch uses it to
// stay quiet -- otherwise pressing HOME in the menu would reboot into the menu.
bool runningFromFactory();

// Sets the boot partition and reboots. Does not return on success; returns
// false if the slot is missing or empty (nothing happens in that case).
bool bootInto(Slot slot);

// Shorthand for `bootInto(Slot::Menu)` -- what the escape hatch calls.
void returnToMenu();

// Starts the background listener that watches both physical buttons.
// Either one, pressed and released, goes home; so does holding one down.
// Runs on its own task, so it still works when the app's main loop is wedged.
// Called for you by board::begin().
void startEscapeHatch();

// Touch-based escape hatch. Apps call this once per loop with their current
// touch state; holding anywhere on screen for `kTouchHoldMs` goes home.
//
// It is fed from the app loop rather than polled in the background task on
// purpose: the touch controller sits on a shared I2C bus that LovyanGFX does
// not guard with a mutex, so only one task may ever read it.
void feedTouchHold(bool touching);

constexpr uint32_t kDebounceMs   = 40;    // ignore contact bounce below this
constexpr uint32_t kMaxTapMs     = 1200;  // longer than this is not a tap
constexpr uint32_t kButtonHoldMs = 1500;  // fires without waiting for release
constexpr uint32_t kTouchHoldMs  = 3000;

}  // namespace app_switch
