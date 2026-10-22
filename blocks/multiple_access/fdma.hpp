#pragma once
/**
 * blocks/multiple_access/fdma.hpp  --  M7: Frequency Division Multiple Access (FDMA)
 *
 * Implements:
 *   - FdmaMux: frequency-division multiplexer mixing K user channels to distinct subcarriers.
 *   - FdmaDemux: downconverts subcarrier k and applies lowpass filtering to recover user k.
 */
#include <algorithm>
#include <cmath>
#include <vector>
#include "../../engine/block.hpp"

namespace blocks {

class FdmaMux : public engine::Block {
public:
    explicit FdmaMux(size_t num_channels = 4, double channel_spacing_hz = 5000.0, double fs = 40000.0)
        : num_channels_(num_channels), channel_spacing_(channel_spacing_hz), fs_(fs) {}

    const char* name() const override { return "FdmaMux"; }

    void work(const std::vector<engine::Stream<engine::cf>*>& in,
              std::vector<engine::Stream<engine::cf>*>& out,
              size_t n) override {
        if (in.empty() || out.empty() || !out[0]) return;
        const size_t in_n = (in[0] && !in[0]->buf.empty()) ? in[0]->buf.size() : n;
        out[0]->resize(in_n);

        auto& y = out[0]->buf;
        std::fill(y.begin(), y.end(), engine::cf(0.0f, 0.0f));

        const double two_pi = 6.28318530717958647692;
        const double dt = 1.0 / fs_;

        const size_t K = std::min(num_channels_, in.size());
        for (size_t u = 0; u < K; ++u) {
            if (!in[u]) continue;
            const auto& user_buf = in[u]->buf;
            const double f_u = (static_cast<double>(u) - static_cast<double>(K - 1) * 0.5) * channel_spacing_;

            for (size_t i = 0; i < in_n && i < user_buf.size(); ++i) {
                const double phase = two_pi * f_u * (sample_count_ + i) * dt;
                const double c_cos = std::cos(phase);
                const double c_sin = std::sin(phase);

                const float re = user_buf[i].real() * static_cast<float>(c_cos) - user_buf[i].imag() * static_cast<float>(c_sin);
                const float im = user_buf[i].real() * static_cast<float>(c_sin) + user_buf[i].imag() * static_cast<float>(c_cos);
                y[i] += engine::cf(re, im);
            }
        }
        sample_count_ += in_n;
    }

    void reset() { sample_count_ = 0; }

private:
    size_t num_channels_ = 4;
    double channel_spacing_ = 5000.0;
    double fs_ = 40000.0;
    uint64_t sample_count_ = 0;
};

class FdmaDemux : public engine::Block {
public:
    explicit FdmaDemux(size_t target_channel = 0, size_t num_channels = 4, double channel_spacing_hz = 5000.0, double fs = 40000.0)
        : target_channel_(target_channel), num_channels_(num_channels), channel_spacing_(channel_spacing_hz), fs_(fs) {}

    const char* name() const override { return "FdmaDemux"; }

    void setTargetChannel(size_t ch) { target_channel_ = ch % num_channels_; }

    void work(const std::vector<engine::Stream<engine::cf>*>& in,
              std::vector<engine::Stream<engine::cf>*>& out,
              size_t n) override {
        if (in.empty() || !in[0] || out.empty() || !out[0]) return;
        const size_t in_n = in[0]->buf.empty() ? n : in[0]->buf.size();
        out[0]->resize(in_n);

        const auto& x = in[0]->buf;
        auto& y = out[0]->buf;

        const double two_pi = 6.28318530717958647692;
        const double dt = 1.0 / fs_;
        const double f_target = (static_cast<double>(target_channel_) - static_cast<double>(num_channels_ - 1) * 0.5) * channel_spacing_;

        // Mix down by -f_target
        std::vector<engine::cf> mixed(in_n);
        for (size_t i = 0; i < in_n; ++i) {
            const double phase = -two_pi * f_target * (sample_count_ + i) * dt;
            const double c_cos = std::cos(phase);
            const double c_sin = std::sin(phase);

            const float re = x[i].real() * static_cast<float>(c_cos) - x[i].imag() * static_cast<float>(c_sin);
            const float im = x[i].real() * static_cast<float>(c_sin) + x[i].imag() * static_cast<float>(c_cos);
            mixed[i] = engine::cf(re, im);
        }

        // Simple moving-average lowpass filter to reject adjacent channel subcarriers
        const size_t filt_len = std::max<size_t>(2, static_cast<size_t>(fs_ / channel_spacing_));
        for (size_t i = 0; i < in_n; ++i) {
            float sum_re = 0.0f;
            float sum_im = 0.0f;
            size_t count = 0;
            for (size_t k = 0; k < filt_len && i >= k; ++k) {
                sum_re += mixed[i - k].real();
                sum_im += mixed[i - k].imag();
                count++;
            }
            y[i] = engine::cf(sum_re / static_cast<float>(count), sum_im / static_cast<float>(count));
        }
        sample_count_ += in_n;
    }

    void reset() { sample_count_ = 0; }

private:
    size_t target_channel_ = 0;
    size_t num_channels_ = 4;
    double channel_spacing_ = 5000.0;
    double fs_ = 40000.0;
    uint64_t sample_count_ = 0;
};

}  // namespace blocks
