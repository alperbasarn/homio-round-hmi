#pragma once
// Value types shared by every model (MDL-02). This header, like everything under model/, has no
// platform includes; the purity guard in test/host rejects any.
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace model {

// Milliseconds on the composition's monotonic clock (see Clock.h).
using Millis = int64_t;

// 0..100. Setpoints, volume, brightness and battery percent are all this type.
using Percent = uint8_t;
constexpr Percent kPercentMax = 100;
constexpr Percent clampPercent(int value) {
    return value < 0 ? Percent{0} : (value > kPercentMax ? kPercentMax : static_cast<Percent>(value));
}

// Degrees Celsius. "No reading" is an explicit sentinel far outside any real temperature;
// the old code used -1.0f, which is a real temperature and was discarded as missing.
using Celsius = float;
constexpr Celsius kNoTemperature = -1000.0f;
inline bool isValidCelsius(Celsius c) {
    return c == c && c > -273.15f && c < 200.0f;  // c == c rejects NaN without <cmath>
}

// Fixed-capacity, always-terminated string. Assigning longer text truncates; nothing allocates.
template <size_t Capacity>
class FixedString {
public:
    static constexpr size_t kCapacity = Capacity;

    FixedString() { clear(); }
    FixedString(const char* text) { assign(text); }  // NOLINT: implicit by design for literals

    void clear() {
        length_ = 0;
        buffer_[0] = '\0';
    }

    bool assign(const char* text) { return assign(text, text ? std::strlen(text) : 0); }

    // Returns false when the input had to be truncated to fit.
    bool assign(const char* text, size_t count) {
        if (text == nullptr) {
            clear();
            return true;
        }
        const bool fits = count <= Capacity;
        const size_t n = fits ? count : Capacity;
        std::memcpy(buffer_, text, n);
        buffer_[n] = '\0';
        length_ = static_cast<uint16_t>(n);
        return fits;
    }

    const char* c_str() const { return buffer_; }
    size_t size() const { return length_; }
    bool empty() const { return length_ == 0; }

    bool operator==(const FixedString& other) const {
        return length_ == other.length_ && std::memcmp(buffer_, other.buffer_, length_) == 0;
    }
    bool operator!=(const FixedString& other) const { return !(*this == other); }

private:
    char buffer_[Capacity + 1];
    uint16_t length_;
};

}  // namespace model
