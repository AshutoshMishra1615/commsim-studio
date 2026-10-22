#pragma once
/**
 * blocks/source/prbs.hpp  --  M1: pseudo-random bit source.
 *
 * Generates bits using the engine's counter-based RNG so the sequence is
 * deterministic and reproducible for any seed/block_id/chunk combination.
 *
 * Output port 0: bits encoded as cf (real = 0.0f or 1.0f, imag = 0).
 */
#include "../../engine/block.hpp"
#include "../../engine/rng.hpp"

namespace blocks {

class PrbsSource : public engine::Block {
public:
    const char* name() const override { return "PrbsSource"; }

    // No inputs. out[0]: bits as cf (real in {0,1}).
    void work(const std::vector<engine::Stream<engine::cf>*>& /*in*/,
              std::vector<engine::Stream<engine::cf>*>& out,
              size_t n) override {
        if (out.empty() || !out[0]) return;
        out[0]->resize(n);
        auto& buf = out[0]->buf;

        // Seed RNG deterministically from (global_seed, block_id, sample_index).
        const auto sample_base =
            static_cast<uint64_t>(params ? params->get("__sample_base", 0.0) : 0.0);
        rng_.seed(global_seed, block_id, sample_base);

        uint64_t word = 0;
        for (size_t i = 0; i < n; ++i) {
            if ((i & 63u) == 0) word = rng_.next_u64();
            buf[i] = engine::cf(static_cast<float>((word >> (i & 63u)) & 1u), 0.f);
        }
    }

private:
    engine::Rng rng_;
};

}  // namespace blocks
