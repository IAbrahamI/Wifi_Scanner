

## 🎯 Project Overview
The goal is to build a self-contained, pocket-sized, hacker-style hardware gadget utilizing the **LILYGO T-Display-S3 Touch**. The device will operate as an independent edge system powered entirely via a smartphone connection (USB-OTG), running custom firmware compiled on a Linux machine (Arch/CachyOS).

---

## 🛠️ Hardware Stack & Physical Architecture

### 1. Core Components
* **Microcontroller / Display Board:** LILYGO T-Display-S3 Touch (ESP32-S3 Dual-Core Xtensa LX7, 16MB Flash, 8MB PSRAM, 1.9" Parallel IPS LCD, CST816 Touch Controller).
* **Power & Data Delivery:** Short, right-angle USB-C to USB-C cable connected to a smartphone utilizing USB On-The-Go (OTG) to stream 5V power.
* **RF Modifications:** Mini U.FL/IPEX to SMA female pigtail adapter routed out to an external high-gain dual-band ($2.4\text{ GHz} / 5\text{ GHz}$) Wi-Fi router antenna to maximize range.

### 2. Enclosure Design (Post-Logic Verification)
* **Manufacturing:** Custom 3D-printed snap-fit enclosure tailored specifically for the **Touch version** panel depth.
* **Mounting Mechanism:** Integrated mechanical solution (e.g., MagSafe magnetic ring or high-strength interlocking fasteners) to securely attach the gadget to the back of a dedicated phone case.
* **Strain Relief:** Hardmount cutouts for the SMA connector to ensure physical forces from the external antenna pull on the shell rather than the PCB.

---

## 💾 Firmware & Memory Management Architecture

### 1. Multi-Boot Partition Map
To host multiple standalone applications simultaneously, the 16MB internal flash will be divided into discrete virtual drives using a custom `partitions.csv` framework:
* **Factory Partition (App 0):** Flipper Zero style touch-driven Main Menu Launcher.
* **OTA Partition 0 (App 1):** Wi-Fi CSI Radar Tracker.
* **OTA Partition 1 (App 2):** Simultaneous Ambient Wi-Fi & BLE Scanner.

### 2. Lifecycle & Handover System
* **Menu-to-App:** The Main Menu uses `esp_ota_set_boot_partition()` to write the target app's address to the boot register and executes `esp_restart()`.
* **App-to-Menu (Escape Hatch):** Every individual application must explicitly contain background listener logic (monitoring for a 3-second touch screen long-press or physical GPIO 0 state drop) to reset the boot register back to the Factory Partition and reboot home.

---

## 🚀 Application Functional Specifications

### Application 1: Wi-Fi Channel State Information (CSI) Radar Tracker
* **Objective:** Passively track human movement through walls via environmental wave distortions without targets needing connection to the radio.
* **Radio Configuration:** Puts the ESP32-S3 radio into promiscuous packet-sniffing mode to catch ambient data waves.
* **Mathematical Constraints:** Uses 128-bit SIMD vector instructions via the `esp-dsp` library to process amplitude and phase shifts across 52 subcarriers in real time.
* **UI Interface:** Renders a fast hardware-accelerated "Turbulence Index" or scrolling phase-variance graph on the ST7789V parallel display using DMA, bypassing SPI latency bottlenecks.

### Application 2: Dual Wi-Fi & BLE Proximity Sniffer
* **Objective:** Map out all nearby active electronic footprints (smartphones, wearables, routers) completely passively.
* **Radio Coexistence Control:** Employs Time Division Multiplexing (TDM) via the ESP32 Software Coexistence manager. Alternates the single internal $2.4\text{ GHz}$ RF front-end between Wi-Fi and Bluetooth tasks in high-speed microsecond windows to prevent memory faults or radio starvation.
* **Scanning Logic:** * **Wi-Fi:** Runs an asynchronous background scan across available channels to harvest SSIDs, BSSIDs, and RSSI metrics.
  * **Bluetooth:** Puts the BLE 5.0 subsystem into an passive observing state to grab ongoing BLE advertising frames (Device Names and RSSI values), working around hardware MAC randomization by focusing on real-time signal volume.
* **UI Interface:** A split-screen real-time dashboard tracking ambient Wi-Fi density charts on the top half, and a scrolling proximity list of surrounding Bluetooth signals on the bottom half.

---

## 💻 Compilation & Deployment Setup (Linux)
* **Toolchain Framework:** PlatformIO IDE extension managed within a localized workspace.
* **Permissions Configuration:** Access to the physical interface (`/dev/ttyACM0` or `/dev/ttyUSB0`) enabled via native system group integration (`uucp`/`dialout`).
* **Hardware Initialization Rule:** Every compiled application binary must forcefully pull the screen's backlight enable pin (**GPIO 15**) to a `HIGH` state inside the initialization sequence to activate the panel's physical power gate.