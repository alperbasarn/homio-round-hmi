#pragma once
#include <cstddef>

namespace model {

// A value with change notification (MDL-04, D3).
//
// Subscribers are a fixed array of plain function pointers plus a context, sized per model at
// compile time; subscribe() returns false when the array is full. set() notifies only when the
// value actually changes (T must provide operator==). Nothing here allocates, so it may be
// called from the render path; NoHeapScope in the host tests asserts that.
template <typename T, size_t MaxSubscribers>
class Observable {
public:
    using Callback = void (*)(const T& value, void* context);
    static constexpr size_t kMaxSubscribers = MaxSubscribers;

    Observable() : value_() {}
    explicit Observable(const T& initial) : value_(initial) {}

    // Returns false when the subscriber array is full or callback is null.
    bool subscribe(Callback callback, void* context) {
        if (callback == nullptr || count_ >= MaxSubscribers) {
            return false;
        }
        subscribers_[count_].callback = callback;
        subscribers_[count_].context = context;
        ++count_;
        return true;
    }

    // Returns true when the value changed (and subscribers were notified).
    bool set(const T& value) {
        if (value == value_) {
            return false;
        }
        value_ = value;
        notify();
        return true;
    }

    const T& get() const { return value_; }
    size_t subscriberCount() const { return count_; }

    void notify() const {
        for (size_t i = 0; i < count_; ++i) {
            subscribers_[i].callback(value_, subscribers_[i].context);
        }
    }

private:
    struct Subscriber {
        Callback callback = nullptr;
        void* context = nullptr;
    };

    Subscriber subscribers_[MaxSubscribers];
    size_t count_ = 0;
    T value_;
};

}  // namespace model
