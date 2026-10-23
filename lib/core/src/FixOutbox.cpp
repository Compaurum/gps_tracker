#include "FixOutbox.h"

namespace core {

FixOutbox::FixOutbox(Fix* storage, size_t capacity) : buf_(storage), cap_(storage ? capacity : 0) {}

void FixOutbox::push(const Fix& fix) {
    if (cap_ == 0) {
        ++dropped_;
        return;
    }
    if (count_ == cap_) {
        popOldest(1);
        ++dropped_;
    }
    buf_[(head_ + count_) % cap_] = fix;
    ++count_;
}

size_t FixOutbox::peekOldest(Fix* out, size_t max, uint32_t* firstId) const {
    const size_t n = max < count_ ? max : count_;
    for (size_t i = 0; i < n; ++i) out[i] = buf_[(head_ + i) % cap_];
    if (firstId) *firstId = headId_;
    return n;
}

void FixOutbox::ack(uint32_t firstId, size_t n) {
    // Ids wrap; compare as signed distances from the oldest queued id.
    const uint32_t endId = firstId + static_cast<uint32_t>(n);
    const int32_t ahead = static_cast<int32_t>(endId - headId_);
    if (ahead <= 0) return;  // all of them were already dropped
    const size_t remove = static_cast<size_t>(ahead) < count_ ? static_cast<size_t>(ahead) : count_;
    popOldest(remove);
}

void FixOutbox::popOldest(size_t n) {
    head_ = (head_ + n) % cap_;
    count_ -= n;
    headId_ += static_cast<uint32_t>(n);
}

}  // namespace core
