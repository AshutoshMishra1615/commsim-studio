/**
 * tests/test_shaping.cpp  --  M4: Pulse shaping, matched filtering, ISI FIR channel,
 *                             and eye diagram tests.
 *
 * PLAN.md §6 Acceptance criteria:
 *   1. Zero ISI at symbol instants: max error < 1e-3 for beta = 0.35.
 *   2. Eye visibly closes with taps [1, 0.5, 0.3].
 *
 * PLAN.md §7 Validation matrix:
 *   Raised-cosine ISI: h(kT) = delta_k for beta in {0.25, 0.35, 0.5, 1.0}.
 */
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>
#include <cstdio>
#include <vector>
#include <numeric>
#include <algorithm>

#include "blocks/shaping/rc_filter.hpp"
#include "blocks/shaping/rrc_filter.hpp"
#include "blocks/shaping/matched_filter.hpp"
#include "blocks/shaping/upsample.hpp"
#include "blocks/shaping/downsample.hpp"
#include "blocks/channel/isi_fir.hpp"
#include "blocks/probe/eye_probe.hpp"
#include "blocks/source/prbs.hpp"
#include "blocks/source/passthrough.hpp"
#include "blocks/mapping/bpsk.hpp"
#include "blocks/channel/awgn.hpp"
#include "engine/graph.hpp"
#include "engine/probe.hpp"

using Catch::Approx;

// Discrete convolution helper
static std::vector<float> convolve(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.empty() || b.empty()) return {};
    std::vector<float> c(a.size() + b.size() - 1, 0.0f);
    for (size_t i = 0; i < a.size(); ++i) {
        for (size_t j = 0; j < b.size(); ++j) {
            c[i + j] += a[i] * b[j];
        }
    }
    return c;
}

TEST_CASE("M4: Raised Cosine Nyquist zero ISI property", "[shaping][rc]") {
    // PLAN.md §7: Raised-cosine ISI: h(kT) = delta_k for beta in {0.25, 0.35, 0.5, 1.0}
    const std::vector<float> betas = {0.25f, 0.35f, 0.5f, 1.0f};
    const size_t sps = 8;
    const size_t span = 8;

    for (float beta : betas) {
        blocks::RcFilter rc(sps, span, beta);
        const auto& taps = rc.taps();
        const size_t center = rc.delay();

        REQUIRE(taps.size() == 2 * span * sps + 1);
        CHECK(taps[center] == Approx(1.0f).margin(1e-5f));

        double max_isi_error = 0.0;
        for (int k = -static_cast<int>(span); k <= static_cast<int>(span); ++k) {
            if (k == 0) continue;
            const size_t idx = center + k * sps;
            const double err = std::abs(static_cast<double>(taps[idx]));
            if (err > max_isi_error) max_isi_error = err;
            CHECK(err < 1e-5);
        }

        std::printf("[M4 RC] beta = %.2f: center = %.6f, max ISI at kT (k!=0): %.2e\n",
                    beta, taps[center], max_isi_error);
    }
}

TEST_CASE("M4: RRC matched filter pair zero ISI acceptance (max error < 1e-3 for beta=0.35)", "[shaping][rrc]") {
    // PLAN.md §6: zero ISI at symbol instants (max error < 1e-3 for beta = 0.35)
    // PLAN.md §7: beta in {0.25, 0.35, 0.5, 1.0}
    const std::vector<float> betas = {0.25f, 0.35f, 0.5f, 1.0f};
    const size_t sps = 8;
    const size_t span = 12;

    for (float beta : betas) {
        blocks::RrcFilter tx_rrc(sps, span, beta);
        blocks::MatchedFilter rx_mf(sps, span, beta);

        // Cascade impulse response = tx_rrc * rx_mf
        const auto cascade = convolve(tx_rrc.taps(), rx_mf.taps());
        const size_t center = tx_rrc.delay() + rx_mf.delay();

        REQUIRE(center < cascade.size());
        const float peak = cascade[center];
        CHECK(peak == Approx(1.0f).margin(1e-3f));

        double max_isi_error = 0.0;
        int max_k = 0;
        const int max_sym_k = static_cast<int>(span);
        for (int k = -max_sym_k; k <= max_sym_k; ++k) {
            if (k == 0) continue;
            const int idx = static_cast<int>(center) + k * static_cast<int>(sps);
            if (idx >= 0 && idx < static_cast<int>(cascade.size())) {
                const double err = std::abs(static_cast<double>(cascade[idx]));
                if (err > max_isi_error) {
                    max_isi_error = err;
                    max_k = k;
                }
            }
        }

        std::printf("[M4 RRC Cascade] beta = %.2f: center_peak = %.6f, max ISI at symbol instants: %.6f (at k = %d)\n",
                    beta, peak, max_isi_error, max_k);

        CHECK(max_isi_error < 1e-3);
        if (std::abs(beta - 0.35f) < 1e-3f) {
            std::printf("  -> ACCEPTANCE CHECK: beta=0.35 max ISI error = %.6f < 1e-3 target: PASSED\n", max_isi_error);
        }
    }
}

TEST_CASE("M4: ISI FIR channel and eye closure acceptance (taps [1, 0.5, 0.3])", "[shaping][isi][eye]") {
    // PLAN.md §6: eye visibly closes with taps [1, 0.5, 0.3]
    const size_t sps = 8;
    const size_t span = 8;
    const float beta = 0.35f;

    auto eye_buf_clean = std::make_shared<engine::TripleBuffer<blocks::EyeSnapshot>>();
    auto eye_buf_isi   = std::make_shared<engine::TripleBuffer<blocks::EyeSnapshot>>();

    blocks::EyeProbe eye_clean(eye_buf_clean, 4096, static_cast<float>(sps));
    blocks::EyeProbe eye_isi(eye_buf_isi, 4096, static_cast<float>(sps));

    // Generate random BPSK symbols
    const size_t num_syms = 512;
    std::vector<engine::cf> syms(num_syms);
    uint32_t lfsr = 0xACE1u;
    for (size_t i = 0; i < num_syms; ++i) {
        lfsr = (lfsr >> 1) ^ (-(lfsr & 1u) & 0xB400u);
        const float b = (lfsr & 1u) ? 1.0f : -1.0f;
        syms[i] = engine::cf{b, 0.0f};
    }

    // Pipeline 1: Upsample -> TX RRC -> RX Matched Filter -> EyeProbe (Clean)
    blocks::Upsample up(sps);
    blocks::RrcFilter tx_rrc(sps, span, beta);
    blocks::MatchedFilter rx_mf(sps, span, beta);

    engine::Stream<engine::cf> sym_stream;
    sym_stream.buf = syms;
    engine::Stream<engine::cf> up_stream;
    engine::Stream<engine::cf> tx_stream;
    engine::Stream<engine::cf> rx_clean_stream;

    std::vector<engine::Stream<engine::cf>*> in1 = {&sym_stream};
    std::vector<engine::Stream<engine::cf>*> out1 = {&up_stream};
    up.work(in1, out1, num_syms);

    std::vector<engine::Stream<engine::cf>*> in2 = {&up_stream};
    std::vector<engine::Stream<engine::cf>*> out2 = {&tx_stream};
    tx_rrc.work(in2, out2, up_stream.buf.size());

    std::vector<engine::Stream<engine::cf>*> in3 = {&tx_stream};
    std::vector<engine::Stream<engine::cf>*> out3 = {&rx_clean_stream};
    rx_mf.work(in3, out3, tx_stream.buf.size());

    std::vector<engine::Stream<engine::cf>*> in_eye_clean = {&rx_clean_stream};
    std::vector<engine::Stream<engine::cf>*> out_eye_clean;
    eye_clean.work(in_eye_clean, out_eye_clean, rx_clean_stream.buf.size());

    const auto& snap_clean = eye_buf_clean->read_buffer();
    // Skip initial transient samples (2 * span * sps = 128 samples) to measure steady state
    const size_t skip_transient = 256;
    const float eye_opening_clean = blocks::EyeProbe::measureEyeOpening(snap_clean, 1.0f, 0.05f, skip_transient);

    // Pipeline 2: Upsample -> TX RRC -> ISI FIR Channel [1.0, 0.5, 0.3] -> RX Matched Filter -> EyeProbe
    blocks::IsiFir isi_channel({1.0f, 0.5f, 0.3f}, sps);
    engine::Stream<engine::cf> isi_stream;
    engine::Stream<engine::cf> rx_isi_stream;

    std::vector<engine::Stream<engine::cf>*> in_isi = {&tx_stream};
    std::vector<engine::Stream<engine::cf>*> out_isi = {&isi_stream};
    isi_channel.work(in_isi, out_isi, tx_stream.buf.size());

    blocks::MatchedFilter rx_mf_isi(sps, span, beta);
    std::vector<engine::Stream<engine::cf>*> in_rx_isi = {&isi_stream};
    std::vector<engine::Stream<engine::cf>*> out_rx_isi = {&rx_isi_stream};
    rx_mf_isi.work(in_rx_isi, out_rx_isi, isi_stream.buf.size());

    std::vector<engine::Stream<engine::cf>*> in_eye_isi = {&rx_isi_stream};
    std::vector<engine::Stream<engine::cf>*> out_eye_isi;
    eye_isi.work(in_eye_isi, out_eye_isi, rx_isi_stream.buf.size());

    const auto& snap_isi = eye_buf_isi->read_buffer();
    const float eye_opening_isi = blocks::EyeProbe::measureEyeOpening(snap_isi, 1.0f, 0.05f, skip_transient);

    std::printf("[M4 Eye Closure Test]\n");
    std::printf("  Clean channel eye opening: %.4f (fully open, theoretical ~ 2.00)\n", eye_opening_clean);
    std::printf("  ISI channel [1, 0.5, 0.3] eye opening: %.4f (severely closed)\n", eye_opening_isi);

    // Theory: Without ISI, BPSK eye opening is ~2.0
    CHECK(eye_opening_clean > 1.70f);
    // With taps [1.0, 0.5, 0.3], worst-case inner level is 1.0 - 0.5 - 0.3 = 0.2, eye opening <= 0.40
    CHECK(eye_opening_isi < 0.45f);
    const float closure_ratio = 1.0f - (eye_opening_isi / eye_opening_clean);
    std::printf("  -> Eye closure ratio: %.1f%% (target: visibly closes, >75%% reduction)\n", closure_ratio * 100.0f);
    CHECK(closure_ratio > 0.75f);
}

TEST_CASE("M4: End-to-end streaming Graph with pulse shaping, matched filter, and downsampling", "[shaping][graph]") {
    const size_t sps = 8;
    const size_t span = 8;
    const float beta = 0.35f;
    const size_t total_filter_delay = 2 * span * sps; // TX delay + RX delay
    const size_t delay_symbols = total_filter_delay / sps; // 16 symbols

    engine::Graph g(512, 42); // 512 symbols per chunk

    auto prbs_id   = g.add(std::make_unique<blocks::PrbsSource>());
    auto fork_id   = g.add(std::make_unique<blocks::PassThrough>());
    auto mod_id    = g.add(std::make_unique<blocks::BpskMod>());
    auto up_id     = g.add(std::make_unique<blocks::Upsample>(sps));
    auto tx_rrc_id = g.add(std::make_unique<blocks::RrcFilter>(sps, span, beta));
    auto rx_mf_id  = g.add(std::make_unique<blocks::MatchedFilter>(sps, span, beta));
    auto down_id   = g.add(std::make_unique<blocks::Downsample>(sps, 0.0f, 0));
    auto demod_id  = g.add(std::make_unique<blocks::BpskDemod>());

    auto probe_block = std::make_unique<engine::BerProbe>();
    probe_block->setDelay(delay_symbols);
    auto* probe_ptr = probe_block.get();
    auto probe_id = g.add(std::move(probe_block));

    g.connect(prbs_id,   0, fork_id,   0);
    g.connect(fork_id,   0, mod_id,    0);
    g.connect(fork_id,   1, probe_id,  0); // TX reference bits
    g.connect(mod_id,    0, up_id,     0);
    g.connect(up_id,     0, tx_rrc_id, 0);
    g.connect(tx_rrc_id, 0, rx_mf_id,  0);
    g.connect(rx_mf_id,  0, down_id,   0);
    g.connect(down_id,   0, demod_id,  0);
    g.connect(demod_id,  0, probe_id,  1); // RX decoded bits

    // Run 200 chunks (102,400 symbols)
    g.run(200);

    const auto snap = probe_ptr->snapshot();
    std::printf("[M4 End-to-End Graph] Processed %lu bits, errors: %lu, BER: %.2e\n",
                snap.total, snap.errors, probe_ptr->ber());

    // In noise-free matched RRC channel with delay compensation, BER must be exactly 0
    CHECK(snap.total > 100000);
    CHECK(snap.errors == 0);
}

TEST_CASE("M4: Timing offset sensitivity slider test", "[shaping][timing]") {
    const size_t sps = 8;
    const size_t span = 8;
    const float beta = 0.35f;

    // Test sampling at optimal instant (offset = 0) vs off-peak instants
    // Offset = 0: Eye fully open, error ~ 0
    // Offset = sps / 2 (sample halfway between symbols): ISI is maximum!
    blocks::Upsample up(sps);
    blocks::RrcFilter tx(sps, span, beta);
    blocks::MatchedFilter rx(sps, span, beta);

    const size_t N = 1000;
    std::vector<engine::cf> syms(N);
    for (size_t i = 0; i < N; ++i) {
        syms[i] = engine::cf((i % 2 == 0) ? 1.0f : -1.0f, 0.0f);
    }

    engine::Stream<engine::cf> s_in; s_in.buf = syms;
    engine::Stream<engine::cf> s_up;
    engine::Stream<engine::cf> s_tx;
    engine::Stream<engine::cf> s_rx;

    std::vector<engine::Stream<engine::cf>*> in1 = {&s_in};
    std::vector<engine::Stream<engine::cf>*> out1 = {&s_up};
    up.work(in1, out1, N);

    std::vector<engine::Stream<engine::cf>*> in2 = {&s_up};
    std::vector<engine::Stream<engine::cf>*> out2 = {&s_tx};
    tx.work(in2, out2, s_up.buf.size());

    std::vector<engine::Stream<engine::cf>*> in3 = {&s_tx};
    std::vector<engine::Stream<engine::cf>*> out3 = {&s_rx};
    rx.work(in3, out3, s_tx.buf.size());

    // Compare downsampling at offset = 0 vs offset = 4 (sps/2)
    blocks::Downsample down_opt(sps, 0.0f, 0);
    blocks::Downsample down_off(sps, static_cast<float>(sps / 2), 0);

    engine::Stream<engine::cf> s_out_opt;
    engine::Stream<engine::cf> s_out_off;

    std::vector<engine::Stream<engine::cf>*> in_down1 = {&s_rx};
    std::vector<engine::Stream<engine::cf>*> out_down1 = {&s_out_opt};
    down_opt.work(in_down1, out_down1, s_rx.buf.size());

    std::vector<engine::Stream<engine::cf>*> in_down2 = {&s_rx};
    std::vector<engine::Stream<engine::cf>*> out_down2 = {&s_out_off};
    down_off.work(in_down2, out_down2, s_rx.buf.size());

    const size_t skip = 2 * span + 5;
    double avg_mag_opt = 0.0;
    double avg_mag_off = 0.0;
    size_t count = 0;
    for (size_t i = skip; i < s_out_opt.buf.size() && i < s_out_off.buf.size(); ++i) {
        avg_mag_opt += std::abs(s_out_opt.buf[i].real());
        avg_mag_off += std::abs(s_out_off.buf[i].real());
        count++;
    }
    avg_mag_opt /= count;
    avg_mag_off /= count;

    std::printf("[M4 Timing Offset Test]\n");
    std::printf("  Optimal sampling (offset = 0): avg magnitude = %.4f (nominal 1.00)\n", avg_mag_opt);
    std::printf("  Off-peak sampling (offset = %zu): avg magnitude = %.4f (attenuated due to transition)\n", sps/2, avg_mag_off);

    CHECK(avg_mag_opt > 0.95);
    CHECK(avg_mag_off < 0.35);
}
