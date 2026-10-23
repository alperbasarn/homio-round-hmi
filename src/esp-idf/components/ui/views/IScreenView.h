#pragma once
// IScreenView (SCRN-03): the lifecycle every view shares. A view owns its LVGL handles and nothing
// else: no mutable domain state, no input polling, no transport (plan, "View Layer").
//
// Lock: every entry point touches LVGL objects, so the caller holds the LVGL lock for the whole
// call (LvglLock.h: a precondition, asserted in debug device builds, compiled out on the host).
//
// build(), destroy() and setVisible() are non-virtual and idempotent: a second build() before
// destroy() does nothing, destroy() of an unbuilt view does nothing, setVisible() repeats nothing.
// The router may therefore call them from any state (SCRN-03: build x2 then destroy x2 leaves no
// assert and a flat heap). The panel size is injected at build time; scalePx() works from it.
//
// The typed half is ScreenView<State>: render(const State&) is the only way model data reaches a
// view, and it reaches nothing while the view is not built.
#include <cstdint>

#include "LvglLock.h"
#include "Theme.h"

#if defined(ESP_PLATFORM)
#include <lvgl.h>
#else
// The host build has no LVGL; the handle stays an opaque pointer.
struct _lv_obj_t;
typedef struct _lv_obj_t lv_obj_t;
#endif

namespace ui {

class IScreenView {
public:
    virtual ~IScreenView() = default;

    // Creates the object tree under `parent` (nullptr: the view creates its own LVGL screen).
    // LVGL objects are visible when created, so a built view reports visible until told otherwise.
    void build(lv_obj_t* parent, int panelWidthPx, int panelHeightPx) {
        QNOB_LVGL_ASSERT_LOCKED();
        if (built_) {
            return;
        }
        width_ = panelWidthPx;
        height_ = panelHeightPx;
        doBuild(parent);
        built_ = true;
        visible_ = true;
    }

    void destroy() {
        QNOB_LVGL_ASSERT_LOCKED();
        if (!built_) {
            return;
        }
        doDestroy();
        built_ = false;
        visible_ = false;
    }

    void setVisible(bool visible) {
        QNOB_LVGL_ASSERT_LOCKED();
        if (!built_ || visible_ == visible) {
            return;
        }
        visible_ = visible;
        doSetVisible(visible);
    }

    virtual lv_obj_t* root() const = 0;

    bool built() const { return built_; }
    bool visible() const { return visible_; }
    int panelWidth() const { return width_; }
    int panelHeight() const { return height_; }
    int scalePx(int referencePx) const { return theme::scalePx(referencePx, width_); }

protected:
    virtual void doBuild(lv_obj_t* parent) = 0;
    virtual void doDestroy() = 0;
    virtual void doSetVisible(bool visible) = 0;

private:
    bool built_ = false;
    bool visible_ = false;
    int width_ = 0;
    int height_ = 0;
};

template <typename State>
class ScreenView : public IScreenView {
public:
    // Lock held by the caller. Nothing happens while the view is not built.
    void render(const State& state) {
        QNOB_LVGL_ASSERT_LOCKED();
        if (!built()) {
            return;
        }
        doRender(state);
    }

protected:
    virtual void doRender(const State& state) = 0;
};

}  // namespace ui
