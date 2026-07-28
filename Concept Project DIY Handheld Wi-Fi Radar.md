# 📡 Project: DIY Handheld Wi-Fi Radar (CSI Tracker)

## 🎯 The Ultimate Goal
To build a highly portable, self-contained, hacker-style gadget (similar to a Flipper Zero, but built as a watch or pocket-sized device) that can passively track human movement and presence through walls using **Wi-Fi Channel State Information (CSI) / Radio Wave Shadows**, completely independent of whether the target targets are connected to the network or carrying a phone.

---

## 🧠 Core Scientific Principles

### 1. Device-Free Sensing (Radio Shadows)
* **The Concept:** The human body is a dense bag of water that absorbs, refracts, and scatters $2.4\text{ GHz}$ and $5\text{ GHz}$ radio frequencies. 
* **The Reality:** You are using Wi-Fi as a **radar system**, not an internet source. Targets do not need to be authenticated to your Wi-Fi, nor do they need any electronic devices on their person. They are detected by the physical "shadows" and wave distortions they cast in the environment.

### 2. Passive Sniffing vs. Active Beacons
* **Passive Sniffing:** The gadget hops into a promiscuous packet-sniffing mode. It silently listens to the ambient data traffic already being broadcasted by neighbors' routers (e.g., streaming 4K, gaming). Your device analyzes how those waves are distorted just before hitting your antenna.
* **Bistatic Radar Beam:** If no ambient Wi-Fi is present, you place a tiny secondary ESP32 "beacon" node on a far wall. It screams blank packets to your handheld device, creating an invisible geometric tripwire.

### 3. Range Capabilities & Limitations
* **3–8 meters:** Optimal Range. Clean data, sharp phase changes, capable of calculating path vectors/direction.
* **8–15 meters:** Maximum Presence Range. Data becomes noisy; registers as raw "environmental turbulence" rather than precise positioning.
* **15+ meters:** Blind Zone. The hardware noise floor of the air swallows up the subtle ripples of a human body.

---

## 🛠️ Hardware Options

### Handheld Devices (Sensing & UI)
Choose one of these pre-built developer platforms to avoid messy custom soldering:

| Device | Form Factor | Chipset | Built-in Perks |
| :--- | :--- | :--- | :--- |
| **LILYGO T-Watch S3** | Smartwatch | ESP32-S3 | 1.54" Touchscreen, LiPo Battery, Watch Strap |
| **M5Stack StickC Plus2**| Gum Packet size | ESP32-PICO | Small Color LCD, Internal Battery, Expansion Hat |
| **M5Stack Cardputer** | Credit Card size | ESP32-S3 | Miniature Keyboard, Color Screen, Magnetic Base |

### Range Multipliers & Relays
* **External Antennas:** To break past the default 3–5m range of an onboard ceramic chip antenna, utilize an ESP32 development board with an **IPEX/U.FL connector** attached to a high-gain dual-band router antenna.
* **The Next-Neighbor Mesh (ESP-NOW):** You can daisy-chain multiple cheap ($5) standalone ESP32 chips down a long hallway or perimeter. They sniff localized neighbor data and relay it back to your handheld watch via the packetless **ESP-NOW** protocol without dropping their sniffing channels.

---

## 💻 Software & Implementation Strategy

### Phase 1: Local PC Prototyping (Docker & RuView)
Before shrinking the software to a watch, verify your data streams on your Arch Linux/CachyOS machine.

1. **Expose Ports Properly:** Ensure both HTTP and UDP telemetry ports are bridged out of Docker:
   ```bash
   docker run -p 3000:3000 -p 5005:5005/udp ruvnet/wifi-densepose:latest