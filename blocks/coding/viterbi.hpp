#pragma once
/**
 * blocks/coding/viterbi.hpp  --  M5: Viterbi decoder (soft LLR and hard decision).
 *
 * Theory:
 *   Maximum-likelihood sequence estimation (MLSE) for the rate-1/2, K=7
 *   convolutional code (octal 171, 133).
 *
 *   - Soft decision: Squared Euclidean branch metric (r - x)^2 on raw
 *     analog baseband channel symbols.
 *   - Hard decision: Hamming branch metric on hard-sliced antipodal bits.
 *
 *   Coding gain at BER 1e-5:
 *     Soft decision: >= 4.5 dB gain (BER <= 1e-5 at Eb/N0 <= 4.3 dB).
 *     Hard decision: >= 2.8 dB gain (BER <= 1e-5 at Eb/N0 <= 6.6 dB).
 */
#include <vector>
#include <limits>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include "../../engine/block.hpp"
#include "conv_k7.hpp"

namespace blocks {

struct Viterbi {
    /**
     * Decode received antipodal symbols.
     * @param rx     Received symbols (+1 for bit 0, -1 for bit 1, with noise).
     *               Length must be 2 * (nInfo + 6).
     * @param nInfo  Number of information bits.
     * @param soft   True for soft Euclidean metric, false for hard decision.
     */
    static std::vector<uint8_t> decode(const std::vector<double>& rx, size_t nInfo, bool soft = true) {
        const size_t T = nInfo + 6;
        if (rx.size() < 2 * T) return {};

        std::vector<uint8_t> prev(T * 64, 0);
        double pm[64];
        double npm[64];
        constexpr double INF = 1e30;

        for (int s = 0; s < 64; ++s) pm[s] = INF;
        pm[0] = 0.0;

        for (size_t t = 0; t < T; ++t) {
            double r0 = rx[2 * t];
            double r1 = rx[2 * t + 1];

            if (!soft) {
                // Hard decision: slice to antipodal {-1, +1}
                r0 = (r0 < 0.0) ? -1.0 : 1.0;
                r1 = (r1 < 0.0) ? -1.0 : 1.0;
            }

            for (int s = 0; s < 64; ++s) npm[s] = INF;

            for (int s = 0; s < 64; ++s) {
                if (pm[s] >= INF) continue;

                for (unsigned b = 0; b < 2; ++b) {
                    const unsigned reg = (b << 6) | static_cast<unsigned>(s);
                    const double x0 = ConvK7::par(reg & ConvK7::G0) ? -1.0 : 1.0;
                    const double x1 = ConvK7::par(reg & ConvK7::G1) ? -1.0 : 1.0;

                    const double d0 = r0 - x0;
                    const double d1 = r1 - x1;
                    const double m = pm[s] + (d0 * d0 + d1 * d1);

                    const unsigned ns = reg >> 1;
                    if (m < npm[ns]) {
                        npm[ns] = m;
                        prev[t * 64 + ns] = static_cast<uint8_t>(s);
                    }
                }
            }

            for (int s = 0; s < 64; ++s) pm[s] = npm[s];
        }

        // Traceback from state 0 (trellis terminated with 6 zero bits)
        std::vector<uint8_t> out(T, 0);
        unsigned cur = 0;
        for (size_t t = T; t-- > 0;) {
            out[t] = static_cast<uint8_t>(cur >> 5);
            cur = prev[t * 64 + cur];
        }

        out.resize(nInfo);
        return out;
    }
};

class ViterbiDecoder : public engine::Block {
public:
    explicit ViterbiDecoder(bool soft = true, size_t frame_bits = 1000, bool bit_input = false)
        : soft_(soft), frame_bits_(frame_bits), bit_input_(bit_input) {}

    const char* name() const override { return "ViterbiDecoder"; }

    void setSoft(bool soft) { soft_ = soft; }
    bool isSoft() const { return soft_; }

    void setBitInput(bool b) { bit_input_ = b; }
    bool isBitInput() const { return bit_input_; }

    void setParam(const std::string& key, double v) override {
        if (key == "soft") {
            soft_ = (v > 0.5);
        } else if (key == "frame_bits" && v >= 1.0) {
            frame_bits_ = static_cast<size_t>(v);
        } else if (key == "bit_input") {
            bit_input_ = (v > 0.5);
        }
        if (params) params->set(key, v);
    }

    void work(const std::vector<engine::Stream<engine::cf>*>& in,
              std::vector<engine::Stream<engine::cf>*>& out,
              size_t n) override {
        if (in.empty() || !in[0] || out.empty() || !out[0]) return;
        const size_t in_n = in[0]->buf.size() > 0 ? in[0]->buf.size() : n;

        const size_t coded_frame = 2 * (frame_bits_ + 6);
        const size_t num_frames = in_n / coded_frame;
        if (num_frames == 0) return;

        out[0]->resize(num_frames * frame_bits_);

        const auto& x = in[0]->buf;
        auto& y = out[0]->buf;

        std::vector<double> rx(coded_frame);
        for (size_t f = 0; f < num_frames; ++f) {
            for (size_t i = 0; i < coded_frame; ++i) {
                if (bit_input_) {
                    // Input is decoded bits in {0.0, 1.0}: bit 0 -> +1.0, bit 1 -> -1.0
                    rx[i] = (x[f * coded_frame + i].real() > 0.5f) ? -1.0 : 1.0;
                } else {
                    // Input is raw channel symbols (BPSK: +1.0 for bit 0, -1.0 for bit 1)
                    rx[i] = static_cast<double>(x[f * coded_frame + i].real());
                }
            }

            const auto decoded = Viterbi::decode(rx, frame_bits_, soft_);
            for (size_t k = 0; k < frame_bits_ && k < decoded.size(); ++k) {
                y[f * frame_bits_ + k] = engine::cf(static_cast<float>(decoded[k]), 0.0f);
            }
        }
    }

private:
    bool soft_ = true;
    size_t frame_bits_ = 1000;
    bool bit_input_ = false;
};

}  // namespace blocks
