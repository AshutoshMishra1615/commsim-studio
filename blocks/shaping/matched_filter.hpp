#pragma once
/**
 * blocks/shaping/matched_filter.hpp  --  M4: Matched filter block.
 *
 * Theory:
 *   For an arbitrary transmit pulse p(t), the matched filter impulse response
 *   that maximizes the received SNR in AWGN is:
 *     h_MF(t) = p*(T - t)  <->  h_MF[n] = conj(p[M - 1 - n])
 *
 *   For real symmetric pulses (e.g. RRC or RC), h_MF[n] = p[n].
 *
 *   Can be constructed with custom taps or configured as an RRC matched filter.
 */
#include <vector>
#include <complex>
#include <algorithm>
#include "../../engine/block.hpp"
#include "rrc_filter.hpp"

namespace blocks {

class MatchedFilter : public engine::Block {
public:
    /// Construct with custom taps (matched filter computes h[n] = conj(pulse[M - 1 - n]))
    explicit MatchedFilter(const std::vector<float>& pulse_taps, bool is_already_matched = false) {
        setPulse(pulse_taps, is_already_matched);
    }

    /// Construct as RRC matched filter
    explicit MatchedFilter(size_t sps = 8, size_t span = 12, float beta = 0.35f) {
        RrcFilter rrc(sps, span, beta);
        taps_ = rrc.taps();
        reset();
    }

    const char* name() const override { return "MatchedFilter"; }

    void setPulse(const std::vector<float>& pulse, bool is_already_matched = false) {
        if (is_already_matched) {
            taps_ = pulse;
        } else {
            taps_.resize(pulse.size());
            for (size_t i = 0; i < pulse.size(); ++i) {
                taps_[i] = pulse[pulse.size() - 1 - i];
            }
        }
        reset();
    }

    const std::vector<float>& taps() const { return taps_; }
    size_t delay() const { return taps_.empty() ? 0 : (taps_.size() - 1) / 2; }

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

private:
    std::vector<float> taps_;
    std::vector<engine::cf> history_;
};

}  // namespace blocks
