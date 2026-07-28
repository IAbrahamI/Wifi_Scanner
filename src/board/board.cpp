#include "board.h"
#include "app_switch.h"

LGFX_TDisplayS3 lcd;

namespace board {

void begin(uint8_t rotation) {
    // Peripheral power gate first. Everything downstream -- panel, touch
    // controller, battery divider -- hangs off this rail, so it has to settle
    // before we talk to any of it.
    pinMode(PIN_POWER_ON, OUTPUT);
    digitalWrite(PIN_POWER_ON, HIGH);
    delay(50);

    pinMode(PIN_BUTTON_BOOT, INPUT_PULLUP);
    pinMode(PIN_BUTTON_1, INPUT_PULLUP);

    lcd.init();
    lcd.setRotation(rotation);
    lcd.setBrightness(200);
    lcd.fillScreen(TFT_BLACK);

    analogReadResolution(12);

    app_switch::startEscapeHatch();
}

uint32_t batteryMilliVolts() {
    // Two samples, because the first read after the ADC idles tends to be low.
    analogReadMilliVolts(PIN_BAT_VOLT);
    return analogReadMilliVolts(PIN_BAT_VOLT) * 2;
}

}  // namespace board
