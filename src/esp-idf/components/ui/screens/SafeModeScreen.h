#pragma once

#include "LvglDisplay.h"
#include <lvgl.h>
#include <string>

/**
 * @brief Full-screen "sad face" overlay shown in safe-mode levels that still
 *        have the display enabled (Rescue = 1, Offline Recovery = 3).
 *
 * Create once and call show() to draw it. Call setStatusText() to update the
 * bottom status line (e.g., the AP SSID / IP printed by ConnectivityManager).
 *
 * Thread-safety: all methods must be called from the LVGL task (display task).
 */
class SafeModeScreen {
public:
    /**
     * @param modeLabel  Short label shown in the UI, e.g. "Rescue Mode".
     *                   Stored internally; caller may free the string after
     *                   construction.
     */
    explicit SafeModeScreen(const char* modeLabel);

    /** Builds the LVGL widget tree and makes this screen the active screen. */
    void show();

    /** Updates the bottom status line (AP SSID, IP, or any short message). */
    void setStatusText(const std::string& text);

private:
    void buildUi();
    int  scalePx(int referencePx) const;

    const char* modeLabel_;       // e.g. "Rescue Mode"
    std::string statusText_;

    lv_obj_t*   root_       = nullptr;
    lv_obj_t*   faceCircle_ = nullptr;
    lv_obj_t*   mouthArc_   = nullptr;
    lv_obj_t*   modeLabel_w = nullptr;  // LVGL widget for mode name
    lv_obj_t*   statusLabel_= nullptr;  // LVGL widget for status text
};
