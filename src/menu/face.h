#pragma once

#include "../board/display.h"

// ---------------------------------------------------------------------------
//  Cyberpunk animated eyes -- the launcher's idle "face".
//
//  A self-contained attract screen: two neon eyes that blink, glance around,
//  shift mood, and glitch, over a scanline/grid backdrop. Purely cosmetic; it
//  owns no radio and holds no state the rest of the app cares about.
//
//  update() advances the animation clock and should be called once per frame;
//  draw() renders the current frame into the shared canvas.
// ---------------------------------------------------------------------------
namespace face {

void begin();
void update();
void draw(LGFX_Sprite& c);

// A touch on the face -- triggers a blink animation.
void poke();

// True while a blink is playing, so the launcher keeps redrawing; false when
// the eye is at rest and the static image needs no repaint.
bool isAnimating();

}  // namespace face
