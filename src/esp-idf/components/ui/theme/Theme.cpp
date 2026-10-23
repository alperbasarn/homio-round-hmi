#include "Theme.h"

namespace ui::theme {

int scalePx(int referencePx, int panelPx) {
    const long scaled = (static_cast<long>(referencePx) * panelPx) / kReferencePx;
    return scaled < 1 ? 1 : static_cast<int>(scaled);
}

}  // namespace ui::theme
