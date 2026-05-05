#include "Buttons.h"

static constexpr uint32_t COL_WHITE = 0xFFFFFF;
static constexpr uint32_t COL_BLACK = 0x000000;
static constexpr uint32_t COL_RED   = 0xFF0000;
static constexpr uint32_t COL_GRAY  = 0x808080;

Button::Button(TouchPanel* touch)
    : touchPanel(touch), x(0), y(0), width(0), height(0),
      isPressed(false), wasPressed(false), isVisible(true),
      normalColor(COL_WHITE), pressedColor(COL_RED) {}

void Button::initialize(int cx, int cy, int bw, int bh) {
    x = cx; y = cy; width = bw; height = bh;
}

void Button::update() {
    if (!isVisible) { isPressed = wasPressed = false; return; }
    wasPressed = isPressed;
    if (touchPanel->isPressed()) {
        int tx = touchPanel->getTouchX(), ty = touchPanel->getTouchY();
        isPressed = (tx >= x - width/2 && tx <= x + width/2 &&
                     ty >= y - height/2 && ty <= y + height/2);
    } else {
        isPressed = false;
    }
}

void Button::hide() {
    if (isVisible) { LvglDisplay::fillCircle(x, y, width/2, COL_BLACK); isVisible = false; }
}
void Button::unhide() { if (!isVisible) { isVisible = true; draw(); } }

bool Button::getHasNewPress()    { return isPressed && !wasPressed; }
bool Button::getHasNewRelease()  { return touchPanel->getHasNewRelease() && wasPressed; }

// ── PlayButton ───────────────────────────────────────────────────────────────

PlayButton::PlayButton(TouchPanel* t) : Button(t) {}
void PlayButton::initialize(int cx, int cy, int s) { Button::initialize(cx, cy, s, s); }
void PlayButton::draw() {
    if (!isVisible) return;
    uint32_t c = isPressed ? pressedColor : normalColor;
    LvglDisplay::fillCircle(x, y, width/2, COL_BLACK);
    LvglDisplay::fillTriangle(x - width/4, y - height/3,
                               x - width/4, y + height/3,
                               x + width/3, y, c);
}

// ── PauseButton ──────────────────────────────────────────────────────────────

PauseButton::PauseButton(TouchPanel* t) : Button(t) {}
void PauseButton::initialize(int cx, int cy, int s) { Button::initialize(cx, cy, s, s); }
void PauseButton::draw() {
    if (!isVisible) return;
    uint32_t c = isPressed ? pressedColor : normalColor;
    LvglDisplay::fillCircle(x, y, width/2, COL_BLACK);
    int bw = width/5, bh = height/2, sp = width/8;
    LvglDisplay::fillRect(x - sp - bw/2, y - bh/2, bw, bh, c);
    LvglDisplay::fillRect(x + sp - bw/2, y - bh/2, bw, bh, c);
}

// ── RewindButton ─────────────────────────────────────────────────────────────

RewindButton::RewindButton(TouchPanel* t) : Button(t) {}
void RewindButton::initialize(int cx, int cy, int s) { Button::initialize(cx, cy, s, s); }
void RewindButton::draw() {
    if (!isVisible) return;
    uint32_t c = isPressed ? pressedColor : normalColor;
    LvglDisplay::fillCircle(x, y, width/2, COL_BLACK);
    int tw = width/3, th = height/2, sp = width/12;
    LvglDisplay::fillTriangle(x + tw/2 - sp, y, x - tw/2 - sp, y - th/2, x - tw/2 - sp, y + th/2, c);
    LvglDisplay::fillTriangle(x - sp, y,       x - tw - sp,    y - th/2, x - tw - sp,    y + th/2, c);
}

// ── ForwardButton ────────────────────────────────────────────────────────────

ForwardButton::ForwardButton(TouchPanel* t) : Button(t) {}
void ForwardButton::initialize(int cx, int cy, int s) { Button::initialize(cx, cy, s, s); }
void ForwardButton::draw() {
    if (!isVisible) return;
    uint32_t c = isPressed ? pressedColor : normalColor;
    LvglDisplay::fillCircle(x, y, width/2, COL_BLACK);
    int tw = width/3, th = height/2, sp = width/12;
    LvglDisplay::fillTriangle(x - tw/2 + sp, y, x + tw/2 + sp, y - th/2, x + tw/2 + sp, y + th/2, c);
    LvglDisplay::fillTriangle(x + sp, y,        x + tw + sp,   y - th/2, x + tw + sp,   y + th/2, c);
}

// ── BackButton ───────────────────────────────────────────────────────────────

BackButton::BackButton(TouchPanel* t) : Button(t) { normalColor = COL_GRAY; }
void BackButton::initialize(int cx, int cy, int bw, int bh) { Button::initialize(cx, cy, bw, bh); }
void BackButton::draw() {
    if (!isVisible) return;
    uint32_t bg = isPressed ? pressedColor : normalColor;
    LvglDisplay::fillEllipse(x, y, width, height, COL_BLACK);
    LvglDisplay::fillEllipse(x, y, width, height, bg);
    LvglDisplay::fillTriangle(x - 5, y, x + 5, y - 10, x + 5, y + 10, COL_WHITE);
}
