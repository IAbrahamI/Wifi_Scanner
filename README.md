# 📡 Handheld RF Gadget — LILYGO T-Display-S3 Touch

Multi-boot firmware for a pocket-sized passive RF scanner. Three independent
apps share the 16 MB flash; a touch launcher in the factory partition decides
which one boots.

| Slot | Partition | Offset | App | Status |
| :--- | :--- | :--- | :--- | :--- |
| 0 | `factory` | `0x010000` | Touch launcher | ✅ built |
| 1 | `ota_0` | `0x210000` | Wi-Fi CSI radar tracker | 🔲 reserved, not implemented |
| 2 | `ota_1` | `0x610000` | Wi-Fi + BLE proximity sniffer | ✅ built |

---

## Setup

```bash
# PlatformIO Core (the VSCode extension bundles this too)
python3 -m venv ~/.venvs/pio && ~/.venvs/pio/bin/pip install platformio
export PATH="$HOME/.venvs/pio/bin:$PATH"

# Serial access on Arch/CachyOS — the port shows up as /dev/ttyACM0
sudo usermod -aG uucp "$USER"   # log out and back in to take effect
```

## Build & flash

Flash the launcher **first** — it is the factory partition, and it is what the
bootloader falls back to when `otadata` is blank.

```bash
pio run -e menu    -t upload    # launcher   -> 0x010000
pio run -e scanner -t upload    # WiFi + BLE -> 0x610000
pio device monitor
```

Each `env` flashes only its own slot, so re-flashing the scanner never disturbs
the launcher. `tools/app_offset.py` handles this — watch for its line in the
build log to confirm:

```
app_offset: menu    -> flashing to 0x10000,  otadata reset to factory
app_offset: scanner -> flashing to 0x610000, otadata untouched
```

That script also suppresses the Arduino platform's `boot_app0.bin`, which it
would otherwise write to `0xe000` on *every* upload. That blob is an otadata
image with `ota_seq = 1`, meaning "boot ota_0" — so without this, uploading the
scanner would leave the device booting the CSI slot instead. Flashing the
launcher blanks otadata (erased otadata → bootloader picks `factory`); flashing
an app leaves the boot register alone.

If the board does not enumerate, hold **BOOT**, tap **RESET**, release BOOT to
force the ROM bootloader, then upload.

## Getting back to the launcher

Every app runs both escape hatches from the spec:

- Hold **BOOT (GPIO 0)** for 1.5 s — handled on its own FreeRTOS task, so it
  works even if the app's main loop is wedged.
- Hold anywhere on the **touchscreen** for 3 s — fed from the app's UI loop.

Both reset the boot register to `factory` and reboot.

---

## Notes on the hardware spec

Three things in the concept docs need adjusting against what this board can
actually do:

**Backlight is GPIO 38, not GPIO 15.** The spec's "Hardware Initialization
Rule" calls for pulling GPIO 15 HIGH. That is correct but incomplete: GPIO 15 is
`PIN_POWER_ON`, the peripheral power gate for the LCD, touch controller and
battery divider. The panel *also* needs GPIO 38 (backlight) driven, or you get a
powered, actively-refreshing, completely black screen. `board::begin()` handles
both — GPIO 15 directly, GPIO 38 via LovyanGFX's PWM backlight driver.

**CSI is 2.4 GHz only.** The ESP32-S3 has no 5 GHz radio at all. A dual-band
antenna is fine to use, but the 5 GHz half is dead weight — all sensing happens
in 2.4 GHz, and range/resolution expectations should be set from that band
alone.

**There is no U.FL connector from the factory.** The external-antenna mod on
T-Display-S3 means moving a 0-ohm resistor on the module to switch the RF feed
away from the onboard ceramic antenna. It is a rework, not a plug-in, and it is
irreversible without more rework. Worth deferring until the firmware side is
proven.

**On CSI feasibility.** The ESP32's CSI output is real and the "radio shadow"
effect is real, but the 3–8 m "path vectors / direction" figure in the concept
doc is optimistic for a single antenna. One RX chain gives you amplitude and
phase variance over time — a good motion/presence signal — but not bearing.
Direction of arrival needs multiple spatially separated receivers, which is what
the ESP-NOW mesh idea in the concept doc would actually buy you. Worth planning
the CSI app around *presence and turbulence* first, and treating localisation as
a later multi-node problem.

---

## Layout

```
platformio.ini              build envs, one per app
partitions_multiapp.csv     16 MB multi-boot map
boards/                     custom board definition (16 MB flash, octal PSRAM)
tools/app_offset.py         redirects each env's upload to its own slot
src/
  board/                    shared: pinout, LovyanGFX+touch driver, app switching
  menu/                     App 0 — launcher
  scanner/                  App 2 — WiFi + BLE sniffer
```

`build_src_filter` in each env compiles `src/board/` plus exactly one app
directory, so the apps stay fully independent binaries.

## Status

Both apps compile clean (PlatformIO Core 6.1.19, espressif32 6.11.0, Arduino
core 2.0.17) and the upload offsets are verified:

| Env | Flash used | Slot size | Upload offset |
| :--- | ---: | ---: | :--- |
| `menu` | 375 KB | 2 MB | `0x010000` ✅ |
| `scanner` | 991 KB | 4 MB | `0x610000` ✅ |

**Not yet run on hardware.** The pinout, LovyanGFX panel config and partition
logic are written from the T-Display-S3 reference but nothing has been flashed,
so the display config in particular (colour inversion, the 35px column offset,
touch axis mapping) is the most likely thing to need a tweak on first boot.

If `pip install platformio` gives you a `ModuleNotFoundError: No module named
'intelhex'` during the bootloader step, `pip install intelhex` into the same
venv — bundled esptool misses that dependency.
