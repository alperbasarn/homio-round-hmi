#include "model/DateFormat.h"

#include <cstddef>
#include <cstring>

namespace model {

namespace {

constexpr const char* kMonths[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                   "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

bool allDigits(const char* text, size_t count) {
    if (count == 0) {
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        if (text[i] < '0' || text[i] > '9') {
            return false;
        }
    }
    return true;
}

// Bounded digit parse; the old std::stoi would throw on an absurd length, this returns 0.
int parseDigits(const char* text, size_t count) {
    if (count == 0 || count > 9) {
        return 0;
    }
    int value = 0;
    for (size_t i = 0; i < count; ++i) {
        value = value * 10 + (text[i] - '0');
    }
    return value;
}

}  // namespace

const char* monthName(int month) {
    if (month >= 1 && month <= 12) {
        return kMonths[month - 1];
    }
    return "---";
}

bool formatDate(const char* slashDate, DateText& out) {
    out.assign("-- ---");
    if (slashDate == nullptr) {
        return false;
    }

    const char* firstSlash = std::strchr(slashDate, '/');
    if (firstSlash == nullptr) {
        return false;
    }
    const char* secondSlash = std::strchr(firstSlash + 1, '/');
    if (secondSlash == nullptr) {
        return false;
    }

    const char* monthText = firstSlash + 1;
    const size_t monthLength = static_cast<size_t>(secondSlash - monthText);
    const char* dayText = secondSlash + 1;
    const size_t dayLengthRaw = std::strlen(dayText);

    if (!allDigits(monthText, monthLength) || !allDigits(dayText, dayLengthRaw)) {
        return false;
    }

    // Strip leading zeros from the day, keeping at least one digit.
    size_t dayStart = 0;
    while (dayLengthRaw - dayStart > 1 && dayText[dayStart] == '0') {
        ++dayStart;
    }
    const size_t dayLength = dayLengthRaw - dayStart;

    const char* month = monthName(parseDigits(monthText, monthLength));

    char buffer[DateText::kCapacity + 1];
    size_t n = 0;
    for (size_t i = 0; i < dayLength && n < DateText::kCapacity; ++i) {
        buffer[n++] = dayText[dayStart + i];
    }
    if (n < DateText::kCapacity) {
        buffer[n++] = ' ';
    }
    for (size_t i = 0; month[i] != '\0' && n < DateText::kCapacity; ++i) {
        buffer[n++] = month[i];
    }
    buffer[n] = '\0';
    out.assign(buffer, n);
    return true;
}

}  // namespace model
