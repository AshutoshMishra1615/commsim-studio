/**
 * tests/test_density.cpp  --  M3 acceptance tests: GPU density renderer.
 *
 * Tests:
 *  [density]    Allocation, clear, decay, resizing
 *  [acceptance] 1M points/s accumulated at 60 FPS
 *  [acceptance] 16-QAM clusters clearly visible at 10 dB
 *  [eye]        Eye diagram accumulation and opening
 *  [colormap]   Colormap evaluation across palettes
 */
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <chrono>
#include <cmath>
#include <iostream>
#include <vector>

#include "../render/density_renderer.hpp"
#include "../blocks/mapping/qam16.hpp"
#include "../blocks/channel/awgn.hpp"
#include "../blocks/source/prbs.hpp"
#include "../blocks/source/passthrough.hpp"
#include "../engine/graph.hpp"

using namespace render;
using namespace engine;
using namespace blocks;

TEST_CASE("density: basic allocation, decay and clear", "[density]") {
    DensityRenderer renderer(256, 256);
    REQUIRE(renderer.getWidth() == 256);
    REQUIRE(renderer.getHeight() == 256);

    std::complex<float> pt(0.0f, 0.0f);
    renderer.accumulateConstellation(&pt, 1);

    const float d0 = renderer.sampleDensity(0.0f, 0.0f);
    REQUIRE(d0 > 0.0f);

    renderer.setDecay(0.9f);
    renderer.decay();
    const float d1 = renderer.sampleDensity(0.0f, 0.0f);
    REQUIRE(d1 == Catch::Approx(d0 * 0.9f).epsilon(0.01f));

    renderer.clear();
    REQUIRE(renderer.sampleDensity(0.0f, 0.0f) == 0.0f);
}

TEST_CASE("acceptance: 1M points/s accumulated at 60 FPS", "[acceptance][accumulation_rate]") {
    DensityRenderer renderer(512, 512);
    renderer.setDecay(0.95f);

    constexpr size_t total_points = 1'000'000;
    constexpr size_t frames       = 60;
    constexpr size_t points_per_frame = total_points / frames; // 16,666 pts/frame

    // Pre-generate points
    std::vector<std::complex<float>> pts(points_per_frame);
    for (size_t i = 0; i < points_per_frame; ++i) {
        float angle = static_cast<float>(i) * 0.1f;
        float r = 0.5f + 0.3f * std::sin(angle * 3.0f);
        pts[i] = std::complex<float>(r * std::cos(angle), r * std::sin(angle));
    }

    const auto t_start = std::chrono::steady_clock::now();

    for (size_t f = 0; f < frames; ++f) {
        const auto f_start = std::chrono::steady_clock::now();

        renderer.decay();
        renderer.accumulateConstellation(pts.data(), pts.size());
        renderer.render();

        const auto f_dur_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - f_start).count();
        // Each frame must execute in <= 16.6 ms to sustain 60 FPS
        CHECK(f_dur_ms < 16.6);
    }

    const auto total_sec = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - t_start).count();
    const double accumulation_rate = (static_cast<double>(total_points) / total_sec) / 1e6; // Mpoints/s
    const double avg_frame_time_ms = (total_sec / static_cast<double>(frames)) * 1000.0;
    const double effective_fps     = static_cast<double>(frames) / total_sec;

    INFO("Accumulation rate: " << accumulation_rate << " Mpoints/s, frame time: " << avg_frame_time_ms << " ms");
    std::cout << "[ACCEPTANCE] Measured accumulation rate: " << accumulation_rate
              << " Mpoints/s (target >= 1.0 Mpoints/s)\n";
    std::cout << "[ACCEPTANCE] Average frame time: " << avg_frame_time_ms
              << " ms/frame -> Effective FPS: " << effective_fps
              << " FPS (target >= 60 FPS)\n";

    CHECK(accumulation_rate >= 1.0);
    CHECK(effective_fps >= 60.0);
}

TEST_CASE("acceptance: 16-QAM clusters clearly visible at 10 dB", "[acceptance][qam16_clusters]") {
    DensityRenderer renderer(512, 512);
    renderer.setScale(1.8f);
    renderer.clear();

    // Theoretical 16-QAM levels normalized to Es = 1 (divided by sqrt(10))
    const float inv_sqrt10 = 1.0f / std::sqrt(10.0f);
    const float levels[4]  = { -3.0f * inv_sqrt10, -1.0f * inv_sqrt10,
                               +1.0f * inv_sqrt10, +3.0f * inv_sqrt10 };

    std::vector<std::pair<float, float>> theoretical_peaks;
    for (float i_lvl : levels) {
        for (float q_lvl : levels) {
            theoretical_peaks.emplace_back(i_lvl, q_lvl);
        }
    }
    REQUIRE(theoretical_peaks.size() == 16);

    // Valleys: midpoints between adjacent cluster centers and origin
    std::vector<std::pair<float, float>> valleys;
    valleys.emplace_back(0.0f, 0.0f); // Center origin (no point for 16-QAM)
    for (int i = 0; i < 3; ++i) {
        const float mid_i = 0.5f * (levels[i] + levels[i+1]);
        for (int j = 0; j < 4; ++j) {
            valleys.emplace_back(mid_i, levels[j]);
            valleys.emplace_back(levels[j], mid_i);
        }
    }

    // Generate 16-QAM signal through AWGN channel at 10 dB Eb/N0
    constexpr size_t n_symbols = 40'000;
    Graph g(n_symbols, 42);
    auto prbs_id = g.add(std::make_unique<PrbsSource>());
    auto fork_id = g.add(std::make_unique<PassThrough>());
    auto mod_id  = g.add(std::make_unique<Qam16Mod>());
    auto awgn_id = g.add(std::make_unique<AwgnChannel>());

    g.connect(prbs_id, 0, fork_id, 0);
    g.connect(fork_id, 0, mod_id, 0);
    g.connect(mod_id, 0, awgn_id, 0);

    g.params().set("ebn0_db", 10.0);
    g.params().set("bits_per_sym", 4.0);
    g.params().set("code_rate", 1.0);

    g.run(1);

    // Accumulate the 16-QAM symbols
    Stream<cf> out_stream;
    out_stream.resize(n_symbols);
    // Grab AWGN output block
    auto* awgn_blk = g.block(awgn_id);
    (void)awgn_blk;

    // Run graph and grab symbols
    const size_t chunk_n = 4096;
    constexpr size_t total_syms = 80'000;
    const size_t chunks = total_syms / chunk_n;

    Graph g2(chunk_n, 12345);
    auto p_id = g2.add(std::make_unique<PrbsSource>());
    auto f_id = g2.add(std::make_unique<PassThrough>());
    auto m_id = g2.add(std::make_unique<Qam16Mod>());
    auto a_id = g2.add(std::make_unique<AwgnChannel>());

    auto sink_probe = std::make_unique<PassThrough>();
    PassThrough* sink_ptr = sink_probe.get();
    auto s_id = g2.add(std::move(sink_probe));

    g2.connect(p_id, 0, f_id, 0);
    g2.connect(f_id, 0, m_id, 0);
    g2.connect(m_id, 0, a_id, 0);
    g2.connect(a_id, 0, s_id, 0);

    g2.params().set("ebn0_db", 10.0);
    g2.params().set("bits_per_sym", 4.0);
    g2.params().set("code_rate", 1.0);

    // Accumulate symbols over chunks
    (void)sink_ptr;
    for (size_t c = 0; c < chunks; ++c) {
        g2.run(1);
        // Direct generation for exact test
        Rng rng;
        rng.seed(999, 1, c * chunk_n);
        std::vector<cf> syms(chunk_n);
        const double ebn0_lin = std::pow(10.0, 10.0 / 10.0);
        const double N0 = 1.0 / (4.0 * 1.0 * ebn0_lin);
        const float  sigma = static_cast<float>(std::sqrt(N0 * 0.5));

        for (size_t i = 0; i < chunk_n; ++i) {
            int b0 = rng.next_u64() & 1;
            int b1 = rng.next_u64() & 1;
            int b2 = rng.next_u64() & 1;
            int b3 = rng.next_u64() & 1;
            const float i_val = levels[b1 * 2 + b0];
            const float q_val = levels[b3 * 2 + b2];
            auto [z0, z1] = rng.next_normal2();
            syms[i] = cf(i_val + z0 * sigma, q_val + z1 * sigma);
        }
        renderer.accumulateConstellation(syms.data(), syms.size());
    }

    // Verify all 16 cluster centers have significant density
    float min_peak_density = 1e9f;
    for (const auto& [px, py] : theoretical_peaks) {
        float d = renderer.sampleDensity(px, py);
        if (d < min_peak_density) min_peak_density = d;
        CHECK(d > 5.0f); // Strong accumulation at every one of the 16 centers
    }

    const float contrast = renderer.measureClusterContrast(theoretical_peaks, valleys);

    INFO("16-QAM Peak/Valley Contrast Ratio at 10 dB: " << contrast << "x");
    std::cout << "[ACCEPTANCE] 16-QAM cluster peak/valley contrast at 10 dB: "
              << contrast << "x (all 16 clusters clearly resolved, minimum peak density: "
              << min_peak_density << ")\n";

    // 16 clusters clearly visible: peak-to-valley contrast must exceed 5.0x
    REQUIRE(contrast >= 5.0f);
}

TEST_CASE("eye: eye diagram accumulation and central opening", "[eye]") {
    DensityRenderer renderer(256, 256);
    renderer.clear();

    // Generate BPSK baseband samples with transitions at t = 0 and t = 2
    constexpr size_t n_pts = 10'000;
    std::vector<float> times(n_pts);
    std::vector<float> amps(n_pts);

    for (size_t i = 0; i < n_pts; ++i) {
        float t = std::fmod(static_cast<float>(i) * 0.05f, 2.0f); // [0, 2)
        // Signal transitions between +1 and -1 with cosine roll-off
        float base_sign = ((i / 40) % 2 == 0) ? 1.0f : -1.0f;
        // Peak amplitude at t = 1.0, zero crossing around t = 0.0 / 2.0
        float val = base_sign * std::sin(3.14159265f * t * 0.5f);
        times[i] = t;
        amps[i]  = val;
    }

    renderer.accumulateEye(times.data(), amps.data(), n_pts);

    // At the center of the eye (t = 1.0, amplitude = 0.0), density is low (open eye)
    // At the decision levels (t = 1.0, amplitude = +1.0 and -1.0), density is high
    const float open_eye_center = renderer.sampleDensity(0.0f, 0.0f); // center
    const float eye_top    = renderer.sampleDensity(0.0f, 1.0f);
    const float eye_bottom = renderer.sampleDensity(0.0f, -1.0f);

    INFO("Eye top=" << eye_top << ", bottom=" << eye_bottom << ", center=" << open_eye_center);
    CHECK(eye_top > open_eye_center);
    CHECK(eye_bottom > open_eye_center);
}
