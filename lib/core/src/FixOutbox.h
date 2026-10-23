#pragma once

#include <cstddef>
#include <cstdint>

#include "Fix.h"

namespace core {

// Ring buffer of fixes waiting for delivery, oldest first. A fix leaves only when the batch
// carrying it is acknowledged (PUBACK); a failed publish leaves the queue untouched.
// When full, the oldest fix is dropped to make room and counted in dropped().
//
// Every fix gets a running id, so ack() removes exactly the fixes that were sent, even if
// some of them were dropped for space in the meantime.
class FixOutbox {
public:
    FixOutbox(Fix* storage, size_t capacity);

    void push(const Fix& fix);

    // Copies up to `max` of the oldest fixes into `out`. Returns how many, and the id of the first.
    size_t peekOldest(Fix* out, size_t max, uint32_t* firstId) const;

    // Removes fixes with ids [firstId, firstId + n) that are still queued.
    void ack(uint32_t firstId, size_t n);

    size_t size() const { return count_; }
    size_t capacity() const { return cap_; }
    bool empty() const { return count_ == 0; }
    uint32_t dropped() const { return dropped_; }

private:
    void popOldest(size_t n);

    Fix* buf_;
    size_t cap_;
    size_t head_ = 0;  // index of the oldest fix
    size_t count_ = 0;
    uint32_t headId_ = 0;  // id of the oldest fix
    uint32_t dropped_ = 0;
};

}  // namespace core
