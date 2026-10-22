#pragma once
/**
 * blocks/shaping/downsample.hpp  --  M4: Downsampler / symbol sampler block.
 *
 * Samples incoming oversampled waveform at the symbol rate (1 sample every sps samples)
 * with a configurable timing offset and delay alignment.
 *
 * Supports integer and fractional timing offsets (using linear interpolation)
 * to demonstrate ISI degradation and eye closure under timing synchronization errors.
 *
 * Parameters:
 *   "sps"           -> samples per symbol (default 8)
 *   "timing_offset" -> fractional timing offset in samples (-sps/2 to +sps/2, default 0.0)
 *   "delay"         -> pipeline / filter delay in samples to skip (default 0)
 */
#include <vector>
#include <cmath>
#include <algorithm>
#include "../../engine/block.hpp"

namespace blocks {

class Downsample : public engine::Block {
public:
    explicit Downsample(size_t sps = 8, float timing_offset = 0.0f, size_t delay = 0)
        : sps_(sps), timing_offset_(timing_offset), delay_(delay) {}

    const char* name() const override { return "Downsample"; }

    void setParam(const std::string& key, double v) override {
        if (key == "sps" && v >= 1.0) {
            sps_ = static_cast<size_t>(v);
        } else if (key == "timing_offset") {
            timing_offset_ = static_cast<float>(v);
        } else if (key == "delay" && v >= 0.0) {
            delay_ = static_cast<size_t>(v);
        }
        if (params) params->set(key, v);
    }

    void setSps(size_t sps) { if (sps >= 1) sps_ = sps; }
    void setTimingOffset(float offset) { timing_offset_ = offset; }
    void setDelay(size_t delay) { delay_ = delay; }

    size_t sps() const { return sps_; }
    float timingOffset() const { return timing_offset_; }
    size_t delay() const { return delay_; }

    void reset() {
        history_.clear();
        total_in_ = 0;
    }

    void work(const std::vector<engine::Stream<engine::cf>*>& in,
              std::vector<engine::Stream<engine::cf>*>& out,
              size_t n) override {
        if (in.empty() || !in[0] || out.empty() || !out[0]) return;

        const size_t in_n = in[0]->buf.size();
        const size_t process_n = in_n > 0 ? in_n : n;
        const auto& x = in[0]->buf;

        // Concatenate leftover history from previous chunk with current input
        std::vector<engine::cf> full_buf;
        full_buf.reserve(history_.size() + process_n);
        full_buf.insert(full_buf.end(), history_.begin(), history_.end());
        full_buf.insert(full_buf.end(), x.begin(), x.begin() + process_n);

        const size_t target_symbols = process_n / sps_;
        out[0]->resize(target_symbols);
        auto& y = out[0]->buf;

        const double sps_d = static_cast<double>(sps_);
        // Base sampling phase: aligns with delay_ and timing_offset_
        const double base_offset = static_cast<double>(delay_) + static_cast<double>(timing_offset_);

        size_t produced = 0;
        size_t last_consumed = 0;

        for (size_t k = 0; k < target_symbols; ++k) {
            const double pos = base_offset + static_cast<double>(total_in_ + k) * sps_d;
            // pos relative to start of full_buf
            const double rel_pos = pos - static_cast<double>(total_consumed_);

            if (rel_pos < 0.0) {
                // Before available samples: output zero
                y[k] = engine::cf{0.0f, 0.0f};
                produced++;
                continue;
            }

            const size_t idx0 = static_cast<size_t>(rel_pos);
            const size_t idx1 = idx0 + 1;

            if (idx1 < full_buf.size()) {
                const float frac = static_cast<float>(rel_pos - static_cast<double>(idx0));
                y[k] = full_buf[idx0] * (1.0f - frac) + full_buf[idx1] * frac;
                last_consumed = idx0;
                produced++;
            } else if (idx0 < full_buf.size()) {
                y[k] = full_buf[idx0];
                last_consumed = idx0;
                produced++;
            } else {
                // Not enough samples yet
                y[k] = engine::cf{0.0f, 0.0f};
                produced++;
            }
        }

        total_in_ += produced;

        // Keep unconsumed tail for next chunk
        const size_t retain_start = (last_consumed + 1 < full_buf.size()) ? (last_consumed + 1) : full_buf.size();
        history_.assign(full_buf.begin() + retain_start, full_buf.end());
        total_consumed_ += retain_start;
    }

private:
    size_t sps_ = 8;
    float timing_offset_ = 0.0f;
    size_t delay_ = 0;

    std::vector<engine::cf> history_;
    uint64_t total_in_ = 0;
    uint64_t total_consumed_ = 0;
};

}  // namespace blocks
