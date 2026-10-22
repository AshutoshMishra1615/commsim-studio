#pragma once
/**
 * blocks/mapping/bpsk.hpp  --  M1: BPSK modulator + demodulator blocks.
 *
 * Theory: Es = 1, bits mapped as  0 → +1+0j, 1 → -1+0j.
 * BER = Q(sqrt(2 Eb/N0))  [Proakis & Salehi, 4th ed., eq 5.2-57]
 *
 * BpskMod:
 *   in[0]  : bits as cf (real ∈ {0,1})
 *   out[0] : BPSK symbols (cf, Es=1)
 *
 * BpskDemod:
 *   in[0]  : received symbols (cf)
 *   out[0] : hard decisions as cf (real ∈ {0,1})
 */
#include "../../engine/block.hpp"

namespace blocks {

// --------------------------------------------------------------------------
class BpskMod : public engine::Block {
public:
    const char* name() const override { return "BpskMod"; }

    void work(const std::vector<engine::Stream<engine::cf>*>& in,
              std::vector<engine::Stream<engine::cf>*>& out,
              size_t n) override {
        if (in.empty() || !in[0] || out.empty() || !out[0]) return;
        out[0]->resize(n);
        const auto& b = in[0]->buf;
        auto& s = out[0]->buf;
        for (size_t i = 0; i < n; ++i)
            s[i] = engine::cf(b[i].real() > 0.5f ? -1.f : 1.f, 0.f);
    }
};

// --------------------------------------------------------------------------
class BpskDemod : public engine::Block {
public:
    const char* name() const override { return "BpskDemod"; }

    void work(const std::vector<engine::Stream<engine::cf>*>& in,
              std::vector<engine::Stream<engine::cf>*>& out,
              size_t n) override {
        if (in.empty() || !in[0] || out.empty() || !out[0]) return;
        out[0]->resize(n);
        const auto& r = in[0]->buf;
        auto& b = out[0]->buf;
        for (size_t i = 0; i < n; ++i)
            b[i] = engine::cf(r[i].real() < 0.f ? 1.f : 0.f, 0.f);
    }
};

}  // namespace blocks
