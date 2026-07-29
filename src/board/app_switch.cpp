#include "app_switch.h"
#include "pins.h"

#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>

namespace app_switch {
namespace {

const esp_partition_t* find(Slot slot) {
    esp_partition_subtype_t sub;
    switch (slot) {
        case Slot::Menu:    sub = ESP_PARTITION_SUBTYPE_APP_FACTORY; break;
        case Slot::Csi:     sub = ESP_PARTITION_SUBTYPE_APP_OTA_0;   break;
        case Slot::Scanner: sub = ESP_PARTITION_SUBTYPE_APP_OTA_1;   break;
        case Slot::Toolkit: sub = ESP_PARTITION_SUBTYPE_APP_OTA_2;   break;
        default:            return nullptr;
    }
    return esp_partition_find_first(ESP_PARTITION_TYPE_APP, sub, nullptr);
}

// Edge-detected, debounced button. Goes home on release after a short press, or
// immediately once held past kButtonHoldMs -- so it works whether you tap it or
// lean on it.
struct Button {
    uint8_t  pin;
    bool     down      = false;
    bool     fired     = false;
    uint32_t lastEdge  = 0;
    uint32_t pressedAt = 0;

    bool poll() {
        bool nowDown = digitalRead(pin) == LOW;  // active low
        uint32_t now = millis();

        if (nowDown != down) {
            if (now - lastEdge < kDebounceMs) return false;
            lastEdge = now;
            down     = nowDown;
            if (nowDown) {
                pressedAt = now;
                fired     = false;
            } else if (!fired && now - pressedAt <= kMaxTapMs) {
                return true;  // released from a tap
            }
            return false;
        }

        if (down && !fired && now - pressedAt >= kButtonHoldMs) {
            fired = true;
            return true;  // held long enough, do not wait for release
        }
        return false;
    }
};

void escapeHatchTask(void*) {
    Button boot{PIN_BUTTON_BOOT};
    Button aux{PIN_BUTTON_1};

    for (;;) {
        if (boot.poll() || aux.poll()) returnToMenu();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

}  // namespace

bool isInstalled(Slot slot) {
    const esp_partition_t* p = find(slot);
    if (!p) return false;

    // An ESP32 app image starts with the magic byte 0xE9. An erased partition
    // reads back as 0xFF, which is how we tell "not flashed yet" from "flashed".
    uint8_t magic = 0;
    if (esp_partition_read(p, 0, &magic, sizeof(magic)) != ESP_OK) return false;
    return magic == 0xE9;
}

bool runningFromFactory() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    return running && running->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY;
}

bool bootInto(Slot slot) {
    const esp_partition_t* p = find(slot);
    if (!p || !isInstalled(slot)) {
        log_e("app_switch: slot %d missing or empty", static_cast<int>(slot));
        return false;
    }
    if (esp_ota_set_boot_partition(p) != ESP_OK) {
        log_e("app_switch: could not set boot partition to %s", p->label);
        return false;
    }
    log_i("app_switch: booting into %s @ 0x%06x", p->label, p->address);
    delay(50);  // let the log drain over USB CDC before the reset
    esp_restart();
    return true;  // unreachable
}

void returnToMenu() {
    // Already home. Rebooting here would just be a confusing flash of black.
    if (runningFromFactory()) return;
    bootInto(Slot::Menu);
}

void startEscapeHatch() {
    xTaskCreatePinnedToCore(escapeHatchTask, "escape", 2560, nullptr,
                            1 /* low priority */, nullptr, 0);
}

void feedTouchHold(bool touching) {
    static uint32_t heldSince = 0;
    if (touching) {
        if (heldSince == 0) heldSince = millis();
        if (millis() - heldSince >= kTouchHoldMs) returnToMenu();
    } else {
        heldSince = 0;
    }
}

}  // namespace app_switch
