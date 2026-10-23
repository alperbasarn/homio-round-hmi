#pragma once
// NullScreenController (BLD-43 / SCRN-04): the controller of a screen that is not split yet, and
// the placeholder the router holds for it. It consumes nothing, requests nothing, and reports the
// screen it stands in for.
#include "ScreenController.h"

namespace controllers {

class NullScreenController final : public ScreenController {
public:
    explicit NullScreenController(model::AppScreen standsFor);

    model::AppScreen screen() const override;

private:
    model::AppScreen screen_;
};

}  // namespace controllers
