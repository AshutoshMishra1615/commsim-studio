#pragma once
/**
 * blocks/mapping/bfsk.hpp  --  M1: Coherent orthogonal BFSK mod/demod.
 *
 * Tone 0 → I axis (1+0j), Tone 1 → Q axis (0+1j). Es=1.
 * BER = Q(sqrt(Eb/N0))  [coherent orthogonal FSK, Proakis & Salehi eq 5.4-46]
 *
 * BfskMod:   in[0] bits → out[0] symbols
 * BfskDemod: in[0] received → out[0] decisions (real ∈ {0,1})
 */
#include "../../engine/block.hpp"

namespace blocks {

class BfskMod : public engine::Block {
public:
    const char* name() const override { return "BfskMod"; }

    void work(const std::vector<engine::Stream<engine::cf>*>& in,
              std::vector<engine::Stream<engine::cf>*>& out,
              size_t n) override {
        if (in.empty() || !in[0] || out.empty() || !out[0]) return;
        out[0]->resize(n);
        const auto& b = in[0]->buf;
        auto& s = out[0]->buf;
        for (size_t i = 0; i < n; ++i)
            s[i] = b[i].real() > 0.5f
                 ? engine::cf(0.f, 1.f)   // bit 1 → Q tone
                 : engine::cf(1.f, 0.f);  // bit 0 → I tone
    }
};

class BfskDemod : public engine::Block {
public:
    const char* name() const override { return "BfskDemod"; }

    void work(const std::vector<engine::Stream<engine::cf>*>& in,
              std::vector<engine::Stream<engine::cf>*>& out,
              size_t n) override {
        if (in.empty() || !in[0] || out.empty() || !out[0]) return;
        out[0]->resize(n);
        const auto& r = in[0]->buf;
        auto& b = out[0]->buf;
        for (size_t i = 0; i < n; ++i)
            b[i] = engine::cf(r[i].imag() > r[i].real() ? 1.f : 0.f, 0.f);
    }
};

}  // namespace blocks
