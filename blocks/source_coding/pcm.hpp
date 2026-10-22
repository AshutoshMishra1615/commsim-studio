#pragma once
/**
 * blocks/source_coding/pcm.hpp  --  M6: Pulse Code Modulation (PCM)
 *
 * Implements:
 *   - Uniform mid-rise quantizer on [-1.0, +1.0] with N bits (2..12 bits).
 *   - PcmQuantizer, PcmEncoder, and PcmDecoder blocks.
 *
 * Theory:
 *   L = 2^N levels, step size Delta = 2 / 2^N.
 *   Quantization noise power for uniform distribution over [-Delta/2, Delta/2] is Delta^2 / 12.
 *   For full-scale sinusoidal input x(t) = sin(2*pi*f*t), Ps = 0.5.
 *   SQNR = 10 * log10(Ps / Pn) = 6.02 * N + 1.76 dB.
 *   [Proakis & Salehi, Communication Systems Engineering, 2nd ed. eq 3.2-18]
 */
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include "../../engine/block.hpp"

namespace blocks {

class PcmCore {
public:
    static inline int encodeSample(double x, int n_bits) {
        const int L = 1 << n_bits;
        const double delta = 2.0 / static_cast<double>(L);
        int idx = static_cast<int>(std::floor((x + 1.0) / delta));
        return std::clamp(idx, 0, L - 1);
    }

    static inline double decodeSample(int idx, int n_bits) {
        const int L = 1 << n_bits;
        const double delta = 2.0 / static_cast<double>(L);
        return -1.0 + (static_cast<double>(idx) + 0.5) * delta;
    }

    static inline double quantize(double x, int n_bits) {
        return decodeSample(encodeSample(x, n_bits), n_bits);
    }
};

// ---------------------------------------------------------------------------
// PcmQuantizer block: passes through quantized analog signal (float -> float)
// ---------------------------------------------------------------------------
class PcmQuantizer : public engine::Block {
public:
    explicit PcmQuantizer(int n_bits = 8) : n_bits_(n_bits) {}

    const char* name() const override { return "PcmQuantizer"; }

    int bits() const { return n_bits_; }
    void setBits(int n) { n_bits_ = std::clamp(n, 1, 16); }

    void setParam(const std::string& key, double v) override {
        if (key == "n_bits" && v >= 1.0) n_bits_ = static_cast<int>(v);
        if (params) params->set(key, v);
    }

    void work(const std::vector<engine::Stream<engine::cf>*>& in,
              std::vector<engine::Stream<engine::cf>*>& out,
              size_t n) override {
        if (in.empty() || !in[0] || out.empty() || !out[0]) return;
        const size_t in_n = in[0]->buf.empty() ? n : in[0]->buf.size();
        out[0]->resize(in_n);

        const auto& x = in[0]->buf;
        auto& y = out[0]->buf;
        for (size_t i = 0; i < in_n; ++i) {
            const float qr = static_cast<float>(PcmCore::quantize(x[i].real(), n_bits_));
            const float qi = static_cast<float>(PcmCore::quantize(x[i].imag(), n_bits_));
            y[i] = engine::cf(qr, qi);
        }
    }

private:
    int n_bits_ = 8;
};

// ---------------------------------------------------------------------------
// PcmEncoder block: converts analog samples to bit stream (1 sample -> N bits)
// ---------------------------------------------------------------------------
class PcmEncoder : public engine::Block {
public:
    explicit PcmEncoder(int n_bits = 8) : n_bits_(n_bits) {}

    const char* name() const override { return "PcmEncoder"; }

    int bits() const { return n_bits_; }
    void setBits(int n) { n_bits_ = std::clamp(n, 1, 16); }

    void work(const std::vector<engine::Stream<engine::cf>*>& in,
              std::vector<engine::Stream<engine::cf>*>& out,
              size_t n) override {
        if (in.empty() || !in[0] || out.empty() || !out[0]) return;
        const size_t in_n = in[0]->buf.empty() ? n : in[0]->buf.size();
        out[0]->resize(in_n * n_bits_);

        const auto& x = in[0]->buf;
        auto& y = out[0]->buf;
        size_t out_idx = 0;
        for (size_t i = 0; i < in_n; ++i) {
            const int idx = PcmCore::encodeSample(x[i].real(), n_bits_);
            for (int b = n_bits_ - 1; b >= 0; --b) {
                const float bit = static_cast<float>((idx >> b) & 1);
                y[out_idx++] = engine::cf(bit, 0.0f);
            }
        }
    }

private:
    int n_bits_ = 8;
};

// ---------------------------------------------------------------------------
// PcmDecoder block: reconstructs analog samples from bit stream (N bits -> 1 sample)
// ---------------------------------------------------------------------------
class PcmDecoder : public engine::Block {
public:
    explicit PcmDecoder(int n_bits = 8) : n_bits_(n_bits) {}

    const char* name() const override { return "PcmDecoder"; }

    int bits() const { return n_bits_; }
    void setBits(int n) { n_bits_ = std::clamp(n, 1, 16); }

    void work(const std::vector<engine::Stream<engine::cf>*>& in,
              std::vector<engine::Stream<engine::cf>*>& out,
              size_t n) override {
        if (in.empty() || !in[0] || out.empty() || !out[0]) return;
        const size_t in_n = in[0]->buf.empty() ? n : in[0]->buf.size();
        const size_t num_samples = in_n / n_bits_;
        out[0]->resize(num_samples);

        const auto& x = in[0]->buf;
        auto& y = out[0]->buf;
        for (size_t i = 0; i < num_samples; ++i) {
            int idx = 0;
            for (int b = 0; b < n_bits_; ++b) {
                const int bit = (x[i * n_bits_ + b].real() > 0.5f) ? 1 : 0;
                idx = (idx << 1) | bit;
            }
            const float s = static_cast<float>(PcmCore::decodeSample(idx, n_bits_));
            y[i] = engine::cf(s, 0.0f);
        }
    }

private:
    int n_bits_ = 8;
};

}  // namespace blocks
