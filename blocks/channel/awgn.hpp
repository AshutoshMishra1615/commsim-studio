#pragma once
/**
 * blocks/channel/awgn.hpp  --  M1: Complex AWGN channel block.
 *
 * Adds complex Gaussian noise with variance N0/2 per dimension.
 * N0 is derived from: N0 = 1 / (k * R * Eb/N0_lin)
 *   where k = bits/symbol, R = code rate (default 1).
 *
 * The RNG is re-seeded from (global_seed, block_id, sample_base) at the
 * start of every work() call → bit-identical for any thread count.
 *
 * Parameter key (set via ParamStore or setParam):
 *   "ebn0_db"  → Eb/N0 in dB  (default 6 dB)
 *   "bits_per_sym" → k         (default 1 for BPSK)
 *   "code_rate"    → R         (default 1.0)
 *
 * in[0] : input symbols (cf)
 * out[0]: output symbols with noise added (cf)
 */
#include <cmath>
#include "../../engine/block.hpp"
#include "../../engine/rng.hpp"

namespace blocks {

class AwgnChannel : public engine::Block {
public:
    const char* name() const override { return "AwgnChannel"; }

    void setParam(const std::string& key, double v) override {
        if (params) params->set(key, v);
    }

    void work(const std::vector<engine::Stream<engine::cf>*>& in,
              std::vector<engine::Stream<engine::cf>*>& out,
              size_t n) override {
        if (in.empty() || !in[0] || out.empty() || !out[0]) return;

        out[0]->resize(n);

        // Read noise parameters
        const double ebn0_db      = params ? params->get("ebn0_db",      6.0) : 6.0;
        const double bits_per_sym = params ? params->get("bits_per_sym", 1.0) : 1.0;
        const double code_rate    = params ? params->get("code_rate",    1.0) : 1.0;
        const double sample_base  = params ? params->get("__sample_base", 0.0) : 0.0;

        const double ebn0_lin = std::pow(10.0, ebn0_db / 10.0);
        // N0 = Es / (k * R * Eb/N0)  with Es=1
        const double N0 = 1.0 / (bits_per_sym * code_rate * ebn0_lin);
        const float  sigma = static_cast<float>(std::sqrt(N0 * 0.5));

        // Seed RNG deterministically
        rng_.seed(global_seed, block_id, static_cast<uint64_t>(sample_base));

        const auto& s = in[0]->buf;
        auto& r = out[0]->buf;
        for (size_t i = 0; i < n; i += 2) {
            auto [z0, z1] = rng_.next_normal2();
            r[i] = s[i] + engine::cf(z0 * sigma, z1 * sigma);
            if (i + 1 < n) {
                auto [z2, z3] = rng_.next_normal2();
                r[i+1] = s[i+1] + engine::cf(z2 * sigma, z3 * sigma);
            }
        }
    }

private:
    engine::Rng rng_;
};

}  // namespace blocks
