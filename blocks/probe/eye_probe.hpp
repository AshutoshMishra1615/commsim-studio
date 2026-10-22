#pragma once
/**
 * blocks/probe/eye_probe.hpp  --  M3: Eye diagram probe block.
 *
 * Captures baseband samples and computes fractional symbol phase
 * t mod (2 * T_sym) for eye diagram density rendering.
 */
#include <vector>
#include <memory>
#include <cmath>
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

    void work(const std::vector<engine::Stream<engine::cf>*>& in,
              std::vector<engine::Stream<engine::cf>*>& out,
              size_t n) override {
        if (in.empty() || !in[0]) return;

        if (!out.empty() && out[0]) {
            out[0]->resize(n);
            out[0]->buf = in[0]->buf;
        }

        if (!buffer_) return;

        const size_t capture = std::min(n, max_capture_);
        auto& snap = buffer_->write_buffer();
        snap.times.resize(capture);
        snap.amplitudes.resize(capture);

        const auto& buf = in[0]->buf;
        const float period = 2.0f * sps_;
        for (size_t i = 0; i < capture; ++i) {
            const float t_raw = static_cast<float>((total_samples_ + i) % static_cast<uint64_t>(std::max(1.0f, period * 1000.0f)));
            snap.times[i]      = std::fmod(t_raw, period) / sps_; // [0, 2)
            snap.amplitudes[i] = buf[i].real();
        }

        total_samples_ += n;
        snap.sequence = ++sequence_;
        buffer_->publish();
    }

    void reset() {
        total_samples_ = 0;
        sequence_ = 0;
    }

private:
    std::shared_ptr<engine::TripleBuffer<EyeSnapshot>> buffer_;
    size_t max_capture_ = 4096;
    float sps_ = 1.0f;
    uint64_t total_samples_ = 0;
    uint64_t sequence_ = 0;
};

}  // namespace blocks
