#pragma once
/**
 * blocks/multiple_access/tdma.hpp  --  M7: Time Division Multiple Access (TDMA)
 *
 * Implements:
 *   - TdmaMux: multiplexes K user channels into sequential time-slots per frame.
 *   - TdmaDemux: extracts user k's time-slot from the incoming TDMA frame.
 *   - Configurable slot size and optional guard intervals.
 */
#include <algorithm>
#include <vector>
#include "../../engine/block.hpp"

namespace blocks {

class TdmaMux : public engine::Block {
public:
    explicit TdmaMux(size_t num_slots = 4, size_t slot_samples = 64, size_t guard_samples = 8)
        : num_slots_(num_slots), slot_samples_(slot_samples), guard_samples_(guard_samples) {}

    const char* name() const override { return "TdmaMux"; }

    size_t numSlots() const { return num_slots_; }
    size_t slotSamples() const { return slot_samples_; }
    size_t guardSamples() const { return guard_samples_; }

    void work(const std::vector<engine::Stream<engine::cf>*>& in,
              std::vector<engine::Stream<engine::cf>*>& out,
              size_t n) override {
        if (in.empty() || out.empty() || !out[0]) return;
        const size_t in_n = (in[0] && !in[0]->buf.empty()) ? in[0]->buf.size() : n;

        const size_t frame_slots = num_slots_;
        const size_t slot_len = slot_samples_ + guard_samples_;
        const size_t num_frames = in_n / slot_samples_;
        if (num_frames == 0) return;

        out[0]->resize(num_frames * frame_slots * slot_len);
        auto& y = out[0]->buf;
        std::fill(y.begin(), y.end(), engine::cf(0.0f, 0.0f));

        for (size_t f = 0; f < num_frames; ++f) {
            for (size_t u = 0; u < frame_slots && u < in.size(); ++u) {
                if (!in[u]) continue;
                const auto& user_buf = in[u]->buf;
                const size_t out_base = f * frame_slots * slot_len + u * slot_len;
                const size_t in_base = f * slot_samples_;

                for (size_t s = 0; s < slot_samples_ && (in_base + s) < user_buf.size(); ++s) {
                    y[out_base + s] = user_buf[in_base + s];
                }
                // Guard samples remain 0.0f
            }
        }
    }

private:
    size_t num_slots_ = 4;
    size_t slot_samples_ = 64;
    size_t guard_samples_ = 8;
};

class TdmaDemux : public engine::Block {
public:
    explicit TdmaDemux(size_t target_slot = 0, size_t num_slots = 4, size_t slot_samples = 64, size_t guard_samples = 8)
        : target_slot_(target_slot), num_slots_(num_slots), slot_samples_(slot_samples), guard_samples_(guard_samples) {}

    const char* name() const override { return "TdmaDemux"; }

    void setTargetSlot(size_t s) { target_slot_ = s % num_slots_; }

    void work(const std::vector<engine::Stream<engine::cf>*>& in,
              std::vector<engine::Stream<engine::cf>*>& out,
              size_t n) override {
        if (in.empty() || !in[0] || out.empty() || !out[0]) return;
        const size_t in_n = in[0]->buf.empty() ? n : in[0]->buf.size();

        const size_t slot_len = slot_samples_ + guard_samples_;
        const size_t frame_len = num_slots_ * slot_len;
        const size_t num_frames = in_n / frame_len;
        if (num_frames == 0) return;

        out[0]->resize(num_frames * slot_samples_);
        const auto& x = in[0]->buf;
        auto& y = out[0]->buf;

        for (size_t f = 0; f < num_frames; ++f) {
            const size_t in_base = f * frame_len + target_slot_ * slot_len;
            const size_t out_base = f * slot_samples_;

            for (size_t s = 0; s < slot_samples_; ++s) {
                y[out_base + s] = x[in_base + s];
            }
        }
    }

private:
    size_t target_slot_ = 0;
    size_t num_slots_ = 4;
    size_t slot_samples_ = 64;
    size_t guard_samples_ = 8;
};

}  // namespace blocks
