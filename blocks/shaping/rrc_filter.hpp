#pragma once
/**
 * blocks/shaping/rrc_filter.hpp  --  M4: Root Raised Cosine (RRC) filter.
 *
 * Transmit and receive pulse-shaping filter.
 * When an RRC transmit filter is paired with an identical RRC receive filter,
 * their cascade H_TX(f) * H_RX(f) = |H_RRC(f)|^2 = H_RC(f) yields a Raised
 * Cosine response with zero ISI at the symbol sampling instants.
 *
 * Energy is normalized such that sum(h[n]^2) = 1.0, ensuring the cascade
 * peak at t=0 has unit gain (h * h)[center] = 1.0.
 *
 * Parameters:
 *   "sps"   -> samples per symbol (default 8)
 *   "span"  -> filter one-sided span in symbols (default 8)
 *   "beta"  -> roll-off factor in [0, 1] (default 0.35)
 */
#include <cmath>
#include <vector>
#include <algorithm>
#include "../../engine/block.hpp"

namespace blocks {

class RrcFilter : public engine::Block {
public:
    explicit RrcFilter(size_t sps = 8, size_t span = 12, float beta = 0.35f)
        : sps_(sps), span_(span), beta_(beta) {
        designFilter();
    }

    const char* name() const override { return "RrcFilter"; }

    void setParam(const std::string& key, double v) override {
        if (key == "sps" && v >= 1.0) {
            sps_ = static_cast<size_t>(v);
            designFilter();
        } else if (key == "span" && v >= 1.0) {
            span_ = static_cast<size_t>(v);
            designFilter();
        } else if (key == "beta" && v >= 0.0 && v <= 1.0) {
            beta_ = static_cast<float>(v);
            designFilter();
        }
        if (params) params->set(key, v);
    }

    size_t sps() const { return sps_; }
    size_t span() const { return span_; }
    float beta() const { return beta_; }
    const std::vector<float>& taps() const { return taps_; }
    size_t delay() const { return span_ * sps_; }

    void reset() {
        history_.assign(taps_.empty() ? 0 : taps_.size() - 1, {0.0f, 0.0f});
    }

    void work(const std::vector<engine::Stream<engine::cf>*>& in,
              std::vector<engine::Stream<engine::cf>*>& out,
              size_t n) override {
        if (in.empty() || !in[0] || out.empty() || !out[0]) return;

        const size_t in_n = in[0]->buf.size();
        const size_t process_n = in_n > 0 ? in_n : n;
        out[0]->resize(process_n);

        const auto& x = in[0]->buf;
        auto& y = out[0]->buf;

        const size_t M = taps_.size();
        if (M == 0) {
            std::copy_n(x.begin(), process_n, y.begin());
            return;
        }

        const size_t hist_len = M - 1;
        if (history_.size() != hist_len) {
            history_.assign(hist_len, {0.0f, 0.0f});
        }

        // Direct-form FIR filtering
        for (size_t i = 0; i < process_n; ++i) {
            engine::cf acc{0.0f, 0.0f};
            for (size_t j = 0; j < M; ++j) {
                engine::cf val;
                if (i >= j) {
                    val = x[i - j];
                } else {
                    val = history_[hist_len - (j - i)];
                }
                acc += val * taps_[j];
            }
            y[i] = acc;
        }

        // Update history
        if (process_n >= hist_len) {
            for (size_t k = 0; k < hist_len; ++k) {
                history_[k] = x[process_n - hist_len + k];
            }
        } else {
            const size_t shift = process_n;
            for (size_t k = 0; k < hist_len - shift; ++k) {
                history_[k] = history_[k + shift];
            }
            for (size_t k = 0; k < shift; ++k) {
                history_[hist_len - shift + k] = x[k];
            }
        }
    }

    static double eval_rrc(double t, double beta) {
        constexpr double PI = 3.14159265358979323846;
        if (std::abs(t) < 1e-9) {
            return 1.0 - beta + (4.0 * beta / PI);
        }
        const double d = 1.0 - (4.0 * beta * t) * (4.0 * beta * t);
        if (std::abs(d) < 1e-6) {
            // L'Hopital limit at t = +/- 1 / (4 * beta)
            const double term1 = (1.0 + 2.0 / PI) * std::sin(PI / (4.0 * beta));
            const double term2 = (1.0 - 2.0 / PI) * std::cos(PI / (4.0 * beta));
            return (beta / std::sqrt(2.0)) * (term1 + term2);
        }
        const double num = std::sin(PI * t * (1.0 - beta)) + 4.0 * beta * t * std::cos(PI * t * (1.0 + beta));
        const double den = PI * t * d;
        return num / den;
    }

private:
    void designFilter() {
        const size_t half = span_ * sps_;
        const size_t num_taps = 2 * half + 1;
        taps_.resize(num_taps);

        double energy = 0.0;
        for (size_t i = 0; i < num_taps; ++i) {
            const double t = static_cast<double>(static_cast<int64_t>(i) - static_cast<int64_t>(half)) / static_cast<double>(sps_);
            const double v = eval_rrc(t, beta_);
            taps_[i] = static_cast<float>(v);
            energy += v * v;
        }

        // Normalize taps to unit energy so cascaded peak (h * h)[0] = 1.0
        if (energy > 1e-12) {
            const double scale = 1.0 / std::sqrt(energy);
            for (auto& tap : taps_) {
                tap = static_cast<float>(tap * scale);
            }
        }

        reset();
    }

    size_t sps_ = 8;
    size_t span_ = 8;
    float beta_ = 0.35f;
    std::vector<float> taps_;
    std::vector<engine::cf> history_;
};

}  // namespace blocks
