#include "NullScreenController.h"

namespace controllers {

NullScreenController::NullScreenController(model::AppScreen standsFor) : screen_(standsFor) {}

model::AppScreen NullScreenController::screen() const {
    return screen_;
}

}  // namespace controllers
