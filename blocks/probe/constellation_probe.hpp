#pragma once
/**
 * blocks/probe/constellation_probe.hpp  --  M3: Constellation probe block.
 *
 * Captures complex symbol streams (I, Q) from in[0] and publishes them
 * into a wait-free TripleBuffer<std::vector<engine::cf>> for density rendering
 * in the constellation viewer panel.
 */
#include <vector>
#include <memory>
#include <algorithm>
#include "../../engine/block.hpp"
#include "../../engine/triple_buffer.hpp"

namespace blocks {

struct ConstellationSnapshot {
    std::vector<engine::cf> symbols;
    uint64_t sequence = 0;
};

class ConstellationProbe : public engine::Block {
public:
    explicit ConstellationProbe(std::shared_ptr<engine::TripleBuffer<ConstellationSnapshot>> buffer,
                                size_t max_capture = 4096)
        : buffer_(std::move(buffer)), max_capture_(max_capture) {}

    const char* name() const override { return "ConstellationProbe"; }

    void work(const std::vector<engine::Stream<engine::cf>*>& in,
              std::vector<engine::Stream<engine::cf>*>& out,
              size_t n) override {
        if (in.empty() || !in[0]) return;

        // Forward to output if connected
        if (!out.empty() && out[0]) {
            out[0]->resize(n);
            out[0]->buf = in[0]->buf;
        }

        if (!buffer_) return;

        const size_t capture = std::min(n, max_capture_);
        auto& snap = buffer_->write_buffer();
        snap.symbols.resize(capture);
        std::copy_n(in[0]->buf.begin(), capture, snap.symbols.begin());
        snap.sequence = ++sequence_;
        buffer_->publish();
    }

    void reset() {
        sequence_ = 0;
    }

private:
    std::shared_ptr<engine::TripleBuffer<ConstellationSnapshot>> buffer_;
    size_t max_capture_ = 4096;
    uint64_t sequence_ = 0;
};

}  // namespace blocks
