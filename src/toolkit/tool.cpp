#include "tool.h"

namespace toolkit {

void drawHeader(LGFX_Sprite& c, const char* title) {
    c.fillRect(0, 0, SCREEN_W, HEADER_H, COL_HDR);
    c.setTextSize(1);
    c.setTextDatum(middle_left);
    c.setTextColor(COL_ACCENT);
    c.drawString("< BACK", 6, HEADER_H / 2 - 1);
    c.setTextDatum(middle_center);
    c.setTextColor(COL_TEXT);
    c.drawString(title, SCREEN_W / 2, HEADER_H / 2 - 1);
}

}  // namespace toolkit
