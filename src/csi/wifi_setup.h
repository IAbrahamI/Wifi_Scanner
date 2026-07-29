#pragma once

#include <Arduino.h>

#include "../board/display.h"

// ---------------------------------------------------------------------------
//  On-device network provisioning: scan, pick, type the password on the touch
//  screen. The result goes straight into NVS.
//
//  The point is that the password never exists as a file. A compiled-in
//  constant ends up in the firmware image, the build directory, and every copy
//  of the .bin flashed onto another board; even serial provisioning puts it in
//  a terminal scrollback. Typed here it goes from the glass into NVS and stops.
//
//  NVS lives in its own flash partition, so it survives firmware updates --
//  `pio run -t upload` rewrites the bootloader, partition table and app, and
//  never touches it. Only a full `esptool erase_flash` clears it.
// ---------------------------------------------------------------------------
namespace wifi_setup {

enum class Stage { Scanning, ApList, Password, Done, Cancelled };

// Begins a fresh scan and opens the picker.
void start();

// Drives the scan. Call from loop().
void poll();

Stage stage();

void draw(LGFX_Sprite& canvas);
void handleTap(int x, int y);

// Valid once stage() == Done.
const char* chosenSsid();
const char* enteredPassword();

// Wipes the entered password from RAM. Call after handing it to NVS.
void clear();

}  // namespace wifi_setup
