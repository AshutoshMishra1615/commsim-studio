#pragma once
/**
 * blocks/probe/eye_probe.hpp  --  M3/M4: Eye diagram probe block.
 *
 * Captures baseband samples and computes fractional symbol phase
 * t mod (2 * T_sym) for eye diagram density rendering.
 *
 * M4 additions:
 *   - Configurable timing-offset slider support
 *   - Eye opening / height measurement at decision instant
 */
#include <vector>
#include <memory>
#include <cmath>
#include <algorithm>
#include "../../engine/block.hpp"
#include "../../engine/triple_buffer.hpp"

namespace blocks {

struct EyeSnapshot {
    std::vector<float> times;
    std::vector<float> amplitudes;
    uint64_t sequence = 0;
};

class EyeProbe : public engine::Block {
public:
    explicit EyeProbe(std::shared_ptr<engine::TripleBuffer<EyeSnapshot>> buffer,
                      size_t max_capture = 4096,
                      float samples_per_sym = 1.0f)
        : buffer_(std::move(buffer)), max_capture_(max_capture), sps_(samples_per_sym) {}

    const char* name() const override { return "EyeProbe"; }

    void setSamplesPerSymbol(float sps) {
        if (sps > 0.0f) sps_ = sps;
    }

    void setTimingOffset(float offset) {
        timing_offset_ = offset;
    }

    float samplesPerSymbol() const { return sps_; }
    float timingOffset() const { return timing_offset_; }

    void setParam(const std::string& key, double v) override {
        if (key == "sps" && v > 0.0) {
            sps_ = static_cast<float>(v);
        } else if (key == "timing_offset") {
            timing_offset_ = static_cast<float>(v);
        }
        if (params) params->set(key, v);
    }

    void work(const std::vector<engine::Stream<engine::cf>*>& in,
              std::vector<engine::Stream<engine::cf>*>& out,
              size_t n) override {
        if (in.empty() || !in[0]) return;

        const size_t in_n = in[0]->buf.size();
        const size_t process_n = in_n > 0 ? in_n : n;

        if (!out.empty() && out[0]) {
            out[0]->resize(process_n);
            out[0]->buf = in[0]->buf;
        }

        if (!buffer_) return;

        const size_t capture = std::min(process_n, max_capture_);
        auto& snap = buffer_->write_buffer();
        snap.times.resize(capture);
        snap.amplitudes.resize(capture);

        const auto& buf = in[0]->buf;
        const float period = 2.0f * sps_;
        for (size_t i = 0; i < capture; ++i) {
            const float t_raw = static_cast<float>((total_samples_ + i) % static_cast<uint64_t>(std::max(1.0f, period * 1000.0f))) + timing_offset_;
            float t_mod = std::fmod(t_raw, period);
            if (t_mod < 0.0f) t_mod += period;
            snap.times[i]      = t_mod / sps_; // [0, 2)
            snap.amplitudes[i] = buf[i].real();
        }

        total_samples_ += process_n;
        snap.sequence = ++sequence_;
        buffer_->publish();
    }

    void reset() {
        total_samples_ = 0;
        sequence_ = 0;
    }

    /**
     * Compute eye opening (vertical distance between lowest positive trajectory
     * and highest negative trajectory) around decision instant t = decision_t.
     * Skips initial transient samples (skip_samples) so filter ramp-up does not skew measurement.
     */
    static float measureEyeOpening(const EyeSnapshot& snap, float decision_t = 1.0f, float tol = 0.08f, size_t skip_samples = 0) {
        float min_pos = 1e9f;
        float max_neg = -1e9f;
        bool has_pos = false;
        bool has_neg = false;

        const size_t start_idx = std::min(skip_samples, snap.times.size());
        for (size_t i = start_idx; i < snap.times.size(); ++i) {
            if (std::abs(snap.times[i] - decision_t) <= tol) {
                const float a = snap.amplitudes[i];
                if (a > 0.0f) {
                    if (a < min_pos) min_pos = a;
                    has_pos = true;
                } else if (a < 0.0f) {
                    if (a > max_neg) max_neg = a;
                    has_neg = true;
                }
            }
        }

        if (!has_pos || !has_neg) return 0.0f;
        return min_pos - max_neg;
    }

private:
    std::shared_ptr<engine::TripleBuffer<EyeSnapshot>> buffer_;
    size_t max_capture_ = 4096;
    float sps_ = 1.0f;
    float timing_offset_ = 0.0f;
    uint64_t total_samples_ = 0;
    uint64_t sequence_ = 0;
};

}  // namespace blocks
