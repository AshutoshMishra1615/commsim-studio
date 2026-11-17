#pragma once
/**
 * blocks/probe/spectrum_probe.hpp  --  M6: Spectrum Probe
 *
 * Implements:
 *   - In-place Cooley-Tukey Radix-2 FFT.
 *   - Hann windowing.
 *   - Two-sided and one-sided Power Spectral Density (PSD) in dB.
 *   - 98% / 99% occupied power bandwidth measurement for Carson's rule validation.
 *   - Thread-safe snapshot for ImPlot spectrum panel in the viewer.
 */
#include <algorithm>
#include <cmath>
#include <complex>
#include <mutex>
#include <vector>
#include "../../engine/block.hpp"

namespace blocks {

class FftCore {
public:
    using c_double = std::complex<double>;

    /// In-place radix-2 Cooley-Tukey FFT. n must be a power of 2.
    static void fft(std::vector<c_double>& a, bool invert = false) {
        const size_t n = a.size();
        if (n <= 1) return;

        // Bit-reversal permutation
        for (size_t i = 1, j = 0; i < n; ++i) {
            size_t bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap(a[i], a[j]);
        }

        const double two_pi = 6.28318530717958647692;
        for (size_t len = 2; len <= n; len <<= 1) {
            const double ang = two_pi / len * (invert ? -1.0 : 1.0);
            const c_double wlen(std::cos(ang), std::sin(ang));
            for (size_t i = 0; i < n; i += len) {
                c_double w(1.0, 0.0);
                for (size_t j = 0; j < len / 2; ++j) {
                    const c_double u = a[i + j];
                    const c_double v = a[i + j + len / 2] * w;
                    a[i + j] = u + v;
                    a[i + j + len / 2] = u - v;
                    w *= wlen;
                }
            }
        }

        if (invert) {
            for (auto& x : a) x /= static_cast<double>(n);
        }
    }

    /// Compute two-sided PSD (centered: -fs/2 to +fs/2) in dB
    static std::vector<float> computePsdDb(const std::vector<engine::cf>& samples,
                                           size_t n_fft,
                                           std::vector<float>* out_freqs = nullptr,
                                           double fs = 1.0) {
        if (samples.empty() || n_fft < 2) return {};

        // Find nearest power of 2 <= n_fft
        size_t n = 1;
        while ((n << 1) <= n_fft) n <<= 1;

        std::vector<c_double> buf(n);
        const double two_pi = 6.28318530717958647692;

        // Hann window & normalisation
        double win_sum2 = 0.0;
        for (size_t i = 0; i < n; ++i) {
            const double w = 0.5 * (1.0 - std::cos(two_pi * i / (n - 1)));
            win_sum2 += w * w;
            const size_t s_idx = (samples.size() >= n) ? (samples.size() - n + i) : (i % samples.size());
            buf[i] = c_double(samples[s_idx].real() * w, samples[s_idx].imag() * w);
        }

        fft(buf, false);

        std::vector<float> psd_db(n);
        if (out_freqs) out_freqs->resize(n);

        const double norm = std::max(1e-12, win_sum2 * n);
        const double df = fs / static_cast<double>(n);

        // fftshift to center DC at index n/2
        for (size_t i = 0; i < n; ++i) {
            const size_t shifted_idx = (i + n / 2) % n;
            const double pwr = (buf[shifted_idx].real() * buf[shifted_idx].real() +
                                buf[shifted_idx].imag() * buf[shifted_idx].imag()) / norm;
            psd_db[i] = static_cast<float>(10.0 * std::log10(std::max(1e-12, pwr)));

            if (out_freqs) {
                (*out_freqs)[i] = static_cast<float>(-0.5 * fs + i * df);
            }
        }
        return psd_db;
    }

    /// Compute occupied power bandwidth (e.g. 98% of total power) in Hz
    static double measurePowerBandwidth(const std::vector<engine::cf>& samples,
                                        size_t n_fft,
                                        double fs,
                                        double power_fraction = 0.98) {
        size_t n = 1;
        while ((n << 1) <= n_fft) n <<= 1;
        if (samples.empty() || n < 4) return 0.0;

        std::vector<c_double> buf(n);
        const double two_pi = 6.28318530717958647692;
        for (size_t i = 0; i < n; ++i) {
            const double w = 0.5 * (1.0 - std::cos(two_pi * i / (n - 1)));
            const size_t s_idx = (samples.size() >= n) ? (samples.size() - n + i) : (i % samples.size());
            buf[i] = c_double(samples[s_idx].real() * w, samples[s_idx].imag() * w);
        }
        fft(buf, false);

        std::vector<double> pwr(n);
        double total_pwr = 0.0;
        for (size_t i = 0; i < n; ++i) {
            const size_t shifted = (i + n / 2) % n;
            pwr[i] = buf[shifted].real() * buf[shifted].real() + buf[shifted].imag() * buf[shifted].imag();
            total_pwr += pwr[i];
        }
        if (total_pwr <= 1e-12) return 0.0;

        const double target_tail = (1.0 - power_fraction) * 0.5 * total_pwr;

        // Cumulative sum from left
        double sum_left = 0.0;
        size_t idx_low = 0;
        for (size_t i = 0; i < n; ++i) {
            sum_left += pwr[i];
            if (sum_left >= target_tail) {
                idx_low = i;
                break;
            }
        }

        // Cumulative sum from right
        double sum_right = 0.0;
        size_t idx_high = n - 1;
        for (size_t i = n - 1; i > 0; --i) {
            sum_right += pwr[i];
            if (sum_right >= target_tail) {
                idx_high = i;
                break;
            }
        }

        const double df = fs / static_cast<double>(n);
        return static_cast<double>(idx_high - idx_low) * df;
    }
};

// ---------------------------------------------------------------------------
// SpectrumProbe: captures stream samples and computes PSD for UI visualization
// ---------------------------------------------------------------------------
class SpectrumProbe : public engine::Block {
public:
    explicit SpectrumProbe(size_t n_fft = 1024, double fs = 20000.0)
        : n_fft_(n_fft), fs_(fs) {
        psd_db_.resize(n_fft_, -100.0f);
        freqs_.resize(n_fft_, 0.0f);
    }

    const char* name() const override { return "SpectrumProbe"; }

    size_t nFft() const { return n_fft_; }
    double fs() const { return fs_; }

    void setParam(const std::string& key, double v) override {
        if (key == "fs" && v > 0.0) fs_ = v;
        if (params) params->set(key, v);
    }

    void work(const std::vector<engine::Stream<engine::cf>*>& in,
              std::vector<engine::Stream<engine::cf>*>& out,
              size_t n) override {
        if (in.empty() || !in[0]) return;
        const auto& s = in[0]->buf;
        const size_t in_n = s.empty() ? n : s.size();
        if (in_n == 0) return;

        if (!out.empty() && out[0]) {
            out[0]->resize(in_n);
            out[0]->buf = in[0]->buf;
        }

        std::vector<float> f_out;
        auto psd = FftCore::computePsdDb(s, n_fft_, &f_out, fs_);

        std::lock_guard<std::mutex> lock(mu_);
        psd_db_ = std::move(psd);
        freqs_ = std::move(f_out);
    }

    struct Snapshot {
        std::vector<float> freqs;
        std::vector<float> psd_db;
    };

    Snapshot snapshot() const {
        std::lock_guard<std::mutex> lock(mu_);
        return { freqs_, psd_db_ };
    }

private:
    size_t n_fft_ = 1024;
    double fs_ = 20000.0;
    mutable std::mutex mu_;
    std::vector<float> psd_db_;
    std::vector<float> freqs_;
};

}  // namespace blocks
