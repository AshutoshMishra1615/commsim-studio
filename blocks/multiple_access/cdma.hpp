#pragma once
/**
 * blocks/multiple_access/cdma.hpp  --  M7: Code Division Multiple Access (CDMA)
 *
 * Implements:
 *   - Orthogonal Walsh-Hadamard code generator (SF = 8, 16, 64, etc.).
 *   - Pseudo-Noise (PN) sequence generator (LFSR m-sequences / random bipolar).
 *   - Multi-user synchronous DS-CDMA transmitter and receiver blocks.
 *
 * Theory:
 *   Walsh codes: <w_i, w_j> = SF * delta_ij.
 *   Zero multi-user interference in synchronous channel.
 *   PN codes: cross-correlation non-zero, MUI variance = (U - 1) / SF.
 *   BER increases monotonically with number of users U.
 */
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include "../../engine/block.hpp"
#include "../../engine/rng.hpp"

namespace blocks {

class CdmaCodes {
public:
    /// Generate Walsh-Hadamard matrix of order N (N = power of 2)
    /// H[i][j] = (-1)^parity(i & j)
    static std::vector<std::vector<double>> walsh(size_t n) {
        std::vector<std::vector<double>> H(n, std::vector<double>(n));
        for (size_t i = 0; i < n; ++i) {
            for (size_t j = 0; j < n; ++j) {
                // Count parity of i & j
                uint32_t v = static_cast<uint32_t>(i & j);
                v ^= v >> 16;
                v ^= v >> 8;
                v ^= v >> 4;
                v &= 0x0F;
                int parity = (0x6996 >> v) & 1;
                H[i][j] = parity ? -1.0 : 1.0;
            }
        }
        return H;
    }

    /// Generate pseudo-noise random bipolar codes (+1.0 / -1.0)
    static std::vector<std::vector<double>> randomPn(size_t num_users, size_t sf, uint64_t seed = 42) {
        engine::Rng rng;
        rng.seed(seed, 0, 0);
        std::vector<std::vector<double>> codes(num_users, std::vector<double>(sf));
        for (size_t u = 0; u < num_users; ++u) {
            for (size_t j = 0; j < sf; ++j) {
                codes[u][j] = (rng.next_u64() & 1u) ? 1.0 : -1.0;
            }
        }
        return codes;
    }

    /// Generate LFSR m-sequence of period 2^M - 1 (e.g. M=4 -> period 15 or M=6 -> period 63)
    static std::vector<double> lfsrMSeq(uint32_t poly, uint32_t init_state, size_t len) {
        std::vector<double> seq(len);
        uint32_t state = init_state ? init_state : 1u;
        for (size_t i = 0; i < len; ++i) {
            const int out_bit = state & 1u;
            seq[i] = out_bit ? -1.0 : 1.0;
            uint32_t feedback = 0;
            uint32_t temp = state & poly;
            while (temp) {
                feedback ^= (temp & 1u);
                temp >>= 1;
            }
            state = (state >> 1) | (feedback << 15);
        }
        return seq;
    }

    /// Generate deterministic pseudo-noise bipolar chip (+1.0 / -1.0) for user u at symbol i, chip j
    static double pnChip(size_t user, size_t symbol_idx, size_t chip_idx) {
        uint64_t x = (static_cast<uint64_t>(user) * 0x9e3779b97f4a7c15ULL)
                   ^ (static_cast<uint64_t>(symbol_idx) * 0xbf58476d1ce4e5b9ULL)
                   ^ (static_cast<uint64_t>(chip_idx) * 0x94d049bb133111ebULL);
        x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
        x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
        x ^= (x >> 31);
        return (x & 1u) ? 1.0 : -1.0;
    }
};

// ---------------------------------------------------------------------------
// CdmaTransmitter: spreads and sums U user bit streams into 1 composite chip stream
// ---------------------------------------------------------------------------
class CdmaTransmitter : public engine::Block {
public:
    explicit CdmaTransmitter(size_t sf = 16, bool use_walsh = true, size_t num_users = 1)
        : sf_(sf), use_walsh_(use_walsh), num_users_(num_users) {
        rebuildCodes();
    }

    const char* name() const override { return "CdmaTransmitter"; }

    size_t sf() const { return sf_; }
    size_t numUsers() const { return num_users_; }
    bool useWalsh() const { return use_walsh_; }

    void setNumUsers(size_t u) {
        num_users_ = std::max<size_t>(1, std::min(u, sf_));
        rebuildCodes();
    }

    void setUseWalsh(bool w) {
        use_walsh_ = w;
        rebuildCodes();
    }

    void setParam(const std::string& key, double v) override {
        if (key == "num_users" && v >= 1.0) setNumUsers(static_cast<size_t>(v));
        else if (key == "use_walsh") setUseWalsh(v > 0.5);
        if (params) params->set(key, v);
    }

    /// in[u]: user u bit stream (cf.real() in {0.0, 1.0})
    /// out[0]: composite chip stream (Es = 1 per active user)
    void work(const std::vector<engine::Stream<engine::cf>*>& in,
              std::vector<engine::Stream<engine::cf>*>& out,
              size_t n) override {
        if (in.empty() || !in[0] || out.empty() || !out[0]) return;
        const size_t in_n = in[0]->buf.empty() ? n : in[0]->buf.size();
        const size_t out_n = in_n * sf_;
        out[0]->resize(out_n);

        auto& s = out[0]->buf;
        std::fill(s.begin(), s.end(), engine::cf(0.0f, 0.0f));

        const double norm = 1.0 / std::sqrt(static_cast<double>(sf_));
        const size_t active_users = std::min(num_users_, in.size());

        for (size_t u = 0; u < active_users; ++u) {
            if (!in[u]) continue;
            const auto& user_bits = in[u]->buf;
            const auto& code = codes_[u % codes_.size()];

            for (size_t i = 0; i < in_n && i < user_bits.size(); ++i) {
                // Bit 0 -> +1.0, Bit 1 -> -1.0
                const double b = (user_bits[i].real() > 0.5f) ? -1.0 : 1.0;
                for (size_t j = 0; j < sf_; ++j) {
                    const double c = use_walsh_ ? code[j] : CdmaCodes::pnChip(u, i, j);
                    const float chip_val = static_cast<float>(b * c * norm);
                    s[i * sf_ + j] += engine::cf(chip_val, 0.0f);
                }
            }
        }
    }

private:
    void rebuildCodes() {
        if (use_walsh_) {
            codes_ = CdmaCodes::walsh(sf_);
        } else {
            codes_ = CdmaCodes::randomPn(sf_, sf_, 42);
        }
    }

    size_t sf_ = 16;
    bool use_walsh_ = true;
    size_t num_users_ = 1;
    std::vector<std::vector<double>> codes_;
};

// ---------------------------------------------------------------------------
// CdmaReceiver: despreads composite chip stream for target user k
// ---------------------------------------------------------------------------
class CdmaReceiver : public engine::Block {
public:
    explicit CdmaReceiver(size_t target_user = 0, size_t sf = 16, bool use_walsh = true)
        : target_user_(target_user), sf_(sf), use_walsh_(use_walsh) {
        rebuildCodes();
    }

    const char* name() const override { return "CdmaReceiver"; }

    size_t targetUser() const { return target_user_; }
    void setTargetUser(size_t u) { target_user_ = u % sf_; }

    void setUseWalsh(bool w) {
        use_walsh_ = w;
        rebuildCodes();
    }

    void work(const std::vector<engine::Stream<engine::cf>*>& in,
              std::vector<engine::Stream<engine::cf>*>& out,
              size_t n) override {
        if (in.empty() || !in[0] || out.empty() || !out[0]) return;
        const size_t in_n = in[0]->buf.empty() ? n : in[0]->buf.size();
        const size_t num_bits = in_n / sf_;
        out[0]->resize(num_bits);

        const auto& r = in[0]->buf;
        auto& b = out[0]->buf;

        const auto& code = codes_[target_user_ % codes_.size()];
        const double norm = 1.0 / std::sqrt(static_cast<double>(sf_));

        for (size_t i = 0; i < num_bits; ++i) {
            double corr = 0.0;
            for (size_t j = 0; j < sf_; ++j) {
                const double c = use_walsh_ ? code[j] : CdmaCodes::pnChip(target_user_, i, j);
                corr += static_cast<double>(r[i * sf_ + j].real()) * c * norm;
            }
            // Decision: corr > 0 -> bit 0, corr < 0 -> bit 1
            const float bit = (corr < 0.0) ? 1.0f : 0.0f;
            b[i] = engine::cf(bit, 0.0f);
        }
    }

private:
    void rebuildCodes() {
        if (use_walsh_) {
            codes_ = CdmaCodes::walsh(sf_);
        } else {
            codes_ = CdmaCodes::randomPn(sf_, sf_, 42);
        }
    }

    size_t target_user_ = 0;
    size_t sf_ = 16;
    bool use_walsh_ = true;
    std::vector<std::vector<double>> codes_;
};

}  // namespace blocks
