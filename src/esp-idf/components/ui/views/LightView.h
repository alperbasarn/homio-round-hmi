#pragma once
// LightView (CTRL-21 + LED-05): the LIGHT screen. The p0-fixed layout (a fill rising with the
// brightness in the swatch colour, the percent, the effect name, a hint) plus what the registry
// adds: the strip name with an online badge, the rim chooser (one arc segment per known strip,
// mine in colour, the selected one bright, an unpaired one grey), and the pairing prompt. Renders
// only what changed since the last pass, so an idle frame touches no LVGL object.
#include "IScreenView.h"
#include "LightScreenState.h"

namespace ui {

class LightView final : public ScreenView<LightScreenState> {
public:
    lv_obj_t* root() const override { return root_; }

protected:
    void doBuild(lv_obj_t* parent) override;
    void doDestroy() override;
    void doSetVisible(bool visible) override;
    void doRender(const LightScreenState& state) override;

private:
    struct Rendered {
        model::LightState light;
        uint8_t used = 0;
        int8_t selected = -1;
        bool paired[model::LedRegistryState::kSlots] = {};
        bool online[model::LedRegistryState::kSlots] = {};
        bool usedSlot[model::LedRegistryState::kSlots] = {};
        uint32_t rgb[model::LedRegistryState::kSlots] = {};
        model::FixedString<model::kLedNameCapacity> name;
        model::LedPairing pairing = model::LedPairing::Idle;
        int dial = 0;
        bool valid = false;
    };
    void renderFill(const model::LightState& light);
    void renderLabels(const LightScreenState& state);
    void renderRim(const model::LedRegistryState& registry);
    void renderPrompt(const model::LedRegistryState& registry, int dial);

    lv_obj_t* root_ = nullptr;
    lv_obj_t* fill_ = nullptr;
    lv_obj_t* levelLabel_ = nullptr;
    lv_obj_t* effectLabel_ = nullptr;
    lv_obj_t* nameLabel_ = nullptr;
    lv_obj_t* badge_ = nullptr;
    lv_obj_t* hintLabel_ = nullptr;
    lv_obj_t* promptLabel_ = nullptr;
    lv_obj_t* rimArcs_[model::LedRegistryState::kSlots] = {};
    Rendered last_;
};

}  // namespace ui
