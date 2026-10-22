#pragma once
/**
 * blocks/probe/waveform_probe.hpp  --  M2: Waveform probe block.
 *
 * Captures time-domain I/Q symbols from input stream in[0] and publishes
 * them into a wait-free lock-free TripleBuffer<WaveformSnapshot> for
 * real-time rendering in the GUI waveform panel.
 *
 * If out[0] is connected, passes in[0] through unmodified.
 */
#include <vector>
#include <memory>
#include "../../engine/block.hpp"
#include "../../engine/triple_buffer.hpp"

namespace blocks {

struct WaveformSnapshot {
    std::vector<float> time;
    std::vector<float> i_data;
    std::vector<float> q_data;
    uint64_t sample_offset = 0;
};

class WaveformProbe : public engine::Block {
public:
    explicit WaveformProbe(std::shared_ptr<engine::TripleBuffer<WaveformSnapshot>> buffer,
                           size_t max_capture = 1024)
        : buffer_(std::move(buffer)), max_capture_(max_capture) {}

    const char* name() const override { return "WaveformProbe"; }

    void work(const std::vector<engine::Stream<engine::cf>*>& in,
              std::vector<engine::Stream<engine::cf>*>& out,
              size_t n) override {
        if (in.empty() || !in[0]) return;

        // Pass through if output connected
        if (!out.empty() && out[0]) {
            out[0]->resize(n);
            out[0]->buf = in[0]->buf;
        }

        if (!buffer_) return;

        const size_t capture_count = std::min(n, max_capture_);
        auto& snap = buffer_->write_buffer();
        snap.time.resize(capture_count);
        snap.i_data.resize(capture_count);
        snap.q_data.resize(capture_count);
        snap.sample_offset = total_captured_;

        const auto& buf = in[0]->buf;
        for (size_t i = 0; i < capture_count; ++i) {
            snap.time[i]   = static_cast<float>(i);
            snap.i_data[i] = buf[i].real();
            snap.q_data[i] = buf[i].imag();
        }

        total_captured_ += n;
        buffer_->publish();
    }

    void reset() {
        total_captured_ = 0;
    }

private:
    std::shared_ptr<engine::TripleBuffer<WaveformSnapshot>> buffer_;
    size_t max_capture_ = 1024;
    uint64_t total_captured_ = 0;
};

}  // namespace blocks
