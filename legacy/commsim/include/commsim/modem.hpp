#pragma once
#include <memory>
#include "common.hpp"
namespace commsim {

// Every modem is normalised to average symbol energy Es = 1.
struct Modem {
    virtual ~Modem() = default;
    virtual const char* name() const = 0;
    virtual int k() const = 0;  // bits per symbol
    virtual std::vector<cd> modulate(const Bits& b) const = 0;
    virtual Bits demodulate(const std::vector<cd>& rx) const = 0;
    virtual double theoryBer(double ebn0_lin) const = 0;
};

struct BPSK : Modem {
    const char* name() const override { return "BPSK"; }
    int k() const override { return 1; }
    std::vector<cd> modulate(const Bits& b) const override {
        std::vector<cd> s(b.size());
        for (size_t i = 0; i < b.size(); ++i) s[i] = cd(b[i] ? -1.0 : 1.0, 0);
        return s;
    }
    Bits demodulate(const std::vector<cd>& r) const override {
        Bits b(r.size());
        for (size_t i = 0; i < r.size(); ++i) b[i] = r[i].real() < 0;
        return b;
    }
    double theoryBer(double e) const override { return Q(std::sqrt(2 * e)); }
};

struct QPSK : Modem {  // Gray: each bit on its own axis
    const char* name() const override { return "QPSK"; }
    int k() const override { return 2; }
    std::vector<cd> modulate(const Bits& b) const override {
        const double a = std::sqrt(0.5);
        std::vector<cd> s(b.size() / 2);
        for (size_t i = 0; i < s.size(); ++i)
            s[i] = cd(b[2 * i] ? -a : a, b[2 * i + 1] ? -a : a);
        return s;
    }
    Bits demodulate(const std::vector<cd>& r) const override {
        Bits b(2 * r.size());
        for (size_t i = 0; i < r.size(); ++i) {
            b[2 * i] = r[i].real() < 0;
            b[2 * i + 1] = r[i].imag() < 0;
        }
        return b;
    }
    double theoryBer(double e) const override { return Q(std::sqrt(2 * e)); }
};

struct QAM16 : Modem {  // Gray per axis: 00->-3, 01->-1, 11->+1, 10->+3
    const char* name() const override { return "16-QAM"; }
    int k() const override { return 4; }
    static double lvl(int b1, int b0) {
        static const double L[4] = {-3, -1, 3, 1};  // index = b1*2+b0
        return L[b1 * 2 + b0] / std::sqrt(10.0);
    }
    std::vector<cd> modulate(const Bits& b) const override {
        std::vector<cd> s(b.size() / 4);
        for (size_t i = 0; i < s.size(); ++i)
            s[i] = cd(lvl(b[4 * i], b[4 * i + 1]), lvl(b[4 * i + 2], b[4 * i + 3]));
        return s;
    }
    static void slice(double x, uint8_t& b1, uint8_t& b0) {
        const double t = 2.0 / std::sqrt(10.0);
        if (x < -t)      { b1 = 0; b0 = 0; }
        else if (x < 0)  { b1 = 0; b0 = 1; }
        else if (x < t)  { b1 = 1; b0 = 1; }
        else             { b1 = 1; b0 = 0; }
    }
    Bits demodulate(const std::vector<cd>& r) const override {
        Bits b(4 * r.size());
        for (size_t i = 0; i < r.size(); ++i) {
            slice(r[i].real(), b[4 * i], b[4 * i + 1]);
            slice(r[i].imag(), b[4 * i + 2], b[4 * i + 3]);
        }
        return b;
    }
    double theoryBer(double e) const override { return 0.375 * std::erfc(std::sqrt(0.4 * e)); }
};

struct BFSK : Modem {  // coherent orthogonal BFSK: tone 0 -> I axis, tone 1 -> Q axis
    const char* name() const override { return "BFSK (coh.)"; }
    int k() const override { return 1; }
    std::vector<cd> modulate(const Bits& b) const override {
        std::vector<cd> s(b.size());
        for (size_t i = 0; i < b.size(); ++i) s[i] = b[i] ? cd(0, 1) : cd(1, 0);
        return s;
    }
    Bits demodulate(const std::vector<cd>& r) const override {
        Bits b(r.size());
        for (size_t i = 0; i < r.size(); ++i) b[i] = r[i].imag() > r[i].real();
        return b;
    }
    double theoryBer(double e) const override { return Q(std::sqrt(e)); }
};

struct OOK : Modem {  // binary ASK (on-off), amplitudes 0 and sqrt(2) -> Es = 1
    const char* name() const override { return "ASK (OOK)"; }
    int k() const override { return 1; }
    std::vector<cd> modulate(const Bits& b) const override {
        std::vector<cd> s(b.size());
        for (size_t i = 0; i < b.size(); ++i) s[i] = cd(b[i] ? std::sqrt(2.0) : 0.0, 0);
        return s;
    }
    Bits demodulate(const std::vector<cd>& r) const override {
        Bits b(r.size());
        for (size_t i = 0; i < r.size(); ++i) b[i] = r[i].real() > std::sqrt(2.0) / 2;
        return b;
    }
    double theoryBer(double e) const override { return Q(std::sqrt(e)); }
};
}  // namespace commsim
