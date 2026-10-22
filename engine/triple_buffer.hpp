#pragma once
/**
 * engine/triple_buffer.hpp  --  M2: Wait-free lock-free triple buffer.
 *
 * Implements a linearizable 3-buffer exchange via a single atomic state word:
 *   - Bits 0..1: clean buffer index (0, 1, or 2)
 *   - Bit 2:     has_new flag (1 if producer has published new data, 0 if consumed)
 *
 * Guarantees:
 *   - Producer never blocks or waits
 *   - Consumer never blocks or waits
 *   - Consumer always reads the most recent complete, untorn snapshot
 *   - Strict monotonicity: consumer never sees older data after newer data
 */
#include <array>
#include <atomic>
#include <cstdint>

namespace engine {

template<typename T>
class TripleBuffer {
public:
    TripleBuffer()
        : state_(2), producer_idx_(0), consumer_idx_(1) {}

    explicit TripleBuffer(const T& init)
        : state_(2), producer_idx_(0), consumer_idx_(1) {
        buffers_[0] = init;
        buffers_[1] = init;
        buffers_[2] = init;
    }

    /// Producer writes into this buffer
    T& write_buffer() noexcept {
        return buffers_[producer_idx_];
    }

    /// Producer publishes its buffer and atomically acquires the previous clean buffer
    void publish() noexcept {
        uint8_t expected = state_.load(std::memory_order_relaxed);
        uint8_t desired;
        do {
            desired = static_cast<uint8_t>(producer_idx_ | 0x04);
        } while (!state_.compare_exchange_weak(expected, desired,
                                               std::memory_order_release,
                                               std::memory_order_relaxed));
        producer_idx_ = expected & 0x03;
    }

    /// Consumer reads the latest buffer.
    /// If a new buffer has been published, consumer atomically swaps to it.
    /// Returns reference to the current consumer buffer.
    const T& read_buffer() noexcept {
        uint8_t expected = state_.load(std::memory_order_relaxed);
        while (expected & 0x04) {
            uint8_t desired = static_cast<uint8_t>(consumer_idx_); // has_new cleared
            if (state_.compare_exchange_weak(expected, desired,
                                             std::memory_order_acquire,
                                             std::memory_order_relaxed)) {
                consumer_idx_ = expected & 0x03;
                break;
            }
        }
        return buffers_[consumer_idx_];
    }

    /// Checks if a new buffer is ready without swapping
    bool has_new() const noexcept {
        return (state_.load(std::memory_order_acquire) & 0x04) != 0;
    }

private:
    std::array<T, 3> buffers_{};
    std::atomic<uint8_t> state_{2}; // clean_idx = 2, has_new = 0
    int producer_idx_{0};
    int consumer_idx_{1};
};

}  // namespace engine
