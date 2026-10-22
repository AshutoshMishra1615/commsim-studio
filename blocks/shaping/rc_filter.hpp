#pragma once
/**
 * blocks/shaping/rc_filter.hpp  --  M4: Raised Cosine (RC) pulse shaping filter.
 *
 * Impulse response:
 *   p_RC(t) = sinc(t / T) * cos(pi * beta * t / T) / (1 - (2 * beta * t / T)^2)
 *
 * Theory:
 *   Satisfies the Nyquist ISI criterion:
 *     p_RC(k * T) = delta_{k,0}  for all k in Z
 *   Zero ISI at all non-zero symbol sampling instants.
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

class RcFilter : public engine::Block {
public:
    explicit RcFilter(size_t sps = 8, size_t span = 8, float beta = 0.35f)
        : sps_(sps), span_(span), beta_(beta) {
        designFilter();
    }

    const char* name() const override { return "RcFilter"; }

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
            // Shift history
            const size_t shift = process_n;
            for (size_t k = 0; k < hist_len - shift; ++k) {
                history_[k] = history_[k + shift];
            }
            for (size_t k = 0; k < shift; ++k) {
                history_[hist_len - shift + k] = x[k];
            }
        }
    }

    static double eval_rc(double t, double beta) {
        constexpr double PI = 3.14159265358979323846;
        if (std::abs(t) < 1e-9) return 1.0;
        const double d = 1.0 - (2.0 * beta * t) * (2.0 * beta * t);
        if (std::abs(d) < 1e-6) {
            // L'Hopital limit at t = +/- 1 / (2 * beta)
            const double sinc_val = std::sin(PI * t) / (PI * t);
            return (PI / 4.0) * sinc_val;
        }
        const double sinc_val = std::sin(PI * t) / (PI * t);
        return sinc_val * std::cos(PI * beta * t) / d;
    }

private:
    void designFilter() {
        const size_t half = span_ * sps_;
        const size_t num_taps = 2 * half + 1;
        taps_.resize(num_taps);

        for (size_t i = 0; i < num_taps; ++i) {
            const double t = static_cast<double>(static_cast<int64_t>(i) - static_cast<int64_t>(half)) / static_cast<double>(sps_);
            taps_[i] = static_cast<float>(eval_rc(t, beta_));
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
