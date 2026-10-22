#pragma once
/**
 * blocks/shaping/upsample.hpp  --  M4: Upsampler block.
 *
 * Expands symbol rate to sample rate by inserting (sps - 1) zeros
 * between incoming symbol impulses:
 *   out[i * sps]     = in[i]
 *   out[i * sps + k] = 0.0,  for 1 <= k < sps
 *
 * Parameters:
 *   "sps" -> samples per symbol (default 8)
 */
#include <vector>
#include <algorithm>
#include "../../engine/block.hpp"

namespace blocks {

class Upsample : public engine::Block {
public:
    explicit Upsample(size_t sps = 8) : sps_(sps) {}

    const char* name() const override { return "Upsample"; }

    void setParam(const std::string& key, double v) override {
        if (key == "sps" && v >= 1.0) {
            sps_ = static_cast<size_t>(v);
        }
        if (params) params->set(key, v);
    }

    void setSps(size_t sps) {
        if (sps >= 1) sps_ = sps;
    }

    size_t sps() const { return sps_; }

    void work(const std::vector<engine::Stream<engine::cf>*>& in,
              std::vector<engine::Stream<engine::cf>*>& out,
              size_t n) override {
        if (in.empty() || !in[0] || out.empty() || !out[0]) return;

        const size_t in_n = in[0]->buf.size();
        const size_t sym_count = in_n > 0 ? in_n : n;
        const size_t out_count = sym_count * sps_;

        out[0]->resize(out_count);

        const auto& x = in[0]->buf;
        auto& y = out[0]->buf;

        // Zero-fill and insert impulses
        std::fill(y.begin(), y.end(), engine::cf{0.0f, 0.0f});
        for (size_t i = 0; i < sym_count; ++i) {
            y[i * sps_] = x[i];
        }
    }

private:
    size_t sps_ = 8;
};

}  // namespace blocks
