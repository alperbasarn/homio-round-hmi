#pragma once
// ui::theme (BLD-43): what every view shares and no view may own. The reference design is laid
// out in a 240 px space; a view receives its panel size at build time and scales from that, so
// scalePx never reads a display singleton (SCRN-10). Colour roles and the type ramp join this
// header as the views are split out (D10: named roles only, a view never names a face).
#include <cstdint>

namespace ui::theme {

constexpr int kReferencePx = 240;

// A reference-space length on a panel `panelPx` wide. Never less than 1, so a hairline stays a
// hairline and an unknown panel (0) still yields drawable geometry.
int scalePx(int referencePx, int panelPx);

}  // namespace ui::theme
