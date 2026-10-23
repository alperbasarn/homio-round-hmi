#pragma once
// Date text rules lifted out of EnvironmentInfoScreen (MDL-10), over FixedString so the render
// path never allocates.
#include "model/ModelTypes.h"

namespace model {

using DateText = FixedString<8>;  // "31 DEC" is six characters

// 1..12 -> "JAN".."DEC"; anything else -> "---" (the old getMonthName).
const char* monthName(int month);

// "YYYY/MM/DD" -> "7 FEB" (leading zeros of the day stripped). Both fields must be non-empty and
// all digits or the result is "-- ---". An out-of-range month keeps the day and shows "---" for
// the month, as before. Returns true when the input was well-formed.
bool formatDate(const char* slashDate, DateText& out);

}  // namespace model
