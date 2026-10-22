/**
 * tests/test_engine.cpp  --  M1 acceptance tests.
 *
 * Tests:
 *  [engine] Graph/scheduler: topo sort, source→sink connection
 *  [engine] Counter-based RNG: seeded identically → same output
 *  [ber]    BPSK BER at 6 dB within ±3σ of Q(sqrt(2*Eb/N0))
 *  [ber]    QPSK BER at 6 dB within ±3σ of Q(sqrt(2*Eb/N0))
 *  [ber]    16-QAM BER at 10 dB within ±3σ of (3/8)*erfc(sqrt(0.4*Eb/N0))
 *  [ber]    BFSK BER at 6 dB within ±3σ of Q(sqrt(Eb/N0))
 *  [ber]    OOK BER at 6 dB within ±3σ of Q(sqrt(Eb/N0))
 *  [det]    BPSK BER CSV bit-identical for 1 vs 8 threads (seed 42)
 *
 * Theory references: PLAN.md §7 Validation matrix.
 * Tolerance: ±3σ binomial, >= 100 errors per point.
 */
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <vector>
#include <thread>
#include <functional>

// Engine headers
#include "../engine/graph.hpp"
#include "../engine/probe.hpp"
#include "../engine/rng.hpp"

// Block headers
#include "../blocks/source/prbs.hpp"
#include "../blocks/mapping/bpsk.hpp"
#include "../blocks/mapping/qpsk.hpp"
#include "../blocks/mapping/qam16.hpp"
#include "../blocks/mapping/bfsk.hpp"
#include "../blocks/mapping/ook.hpp"
#include "../blocks/channel/awgn.hpp"

using namespace engine;
using namespace blocks;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Q-function: Q(x) = erfc(x/sqrt(2)) / 2
static double Q(double x) {
    return 0.5 * std::erfc(x / std::sqrt(2.0));
}

static double db2lin(double db) { return std::pow(10.0, db / 10.0); }

/// 3-sigma tolerance for a binomial with n samples and true probability p.
/// Returns the absolute half-width: 3 * sqrt(p*(1-p)/n)
static double sigma3(double p, size_t n) {
    return 3.0 * std::sqrt(p * (1.0 - p) / static_cast<double>(n));
}

// ---------------------------------------------------------------------------
// Helper: run a BPSK BER simulation using the engine graph for N total bits.
// Returns measured BER. The graph path is:
//   PrbsSource → BpskMod → AwgnChannel → BpskDemod → BerProbe(rx)
// BerProbe also receives tx bits from PrbsSource directly.
// To avoid the complexity of forking the graph, we run it twice with the same
// seed and compare, or we wire the graph so the probe sees both.
//
// Simplest wiring for M1: a "ForwardBits" block that passes bits through AND
// copies them to a second output so the probe can compare. We implement it
// inline here.
// ---------------------------------------------------------------------------

/// PassThrough block: copies in[0] → out[0] (and optionally out[1]).
class PassThrough : public Block {
public:
    const char* name() const override { return "PassThrough"; }
    void work(const std::vector<Stream<cf>*>& in,
              std::vector<Stream<cf>*>& out,
              size_t n) override {
        if (in.empty() || !in[0]) return;
        for (auto* o : out) {
            if (!o) continue;
            o->resize(n);
            o->buf = in[0]->buf;
        }
    }
};

// ---------------------------------------------------------------------------
// Build a standard BER measurement graph for a given modem type.
// Template Mod and Demod must be constructible with no args.
// Returns the measured BER over `total_bits` bits.
// bits_per_sym: needed to set Eb/N0 correctly in AWGN block.
// ---------------------------------------------------------------------------

struct BerResult { double ber; uint64_t errors; uint64_t total; };

template<class Mod, class Demod>
BerResult measureBer(double ebn0_db, size_t total_bits, uint64_t seed,
                     int bits_per_sym = 1) {
    // chunk_size must be a multiple of bits_per_sym
    const size_t chunk  = 4096 * static_cast<size_t>(bits_per_sym);
    const size_t chunks = (total_bits + chunk - 1) / chunk;

    Graph g(chunk, seed);

    // 0: PRBS source
    auto prbs_id = g.add(std::make_unique<PrbsSource>());
    // 1: Passthrough to fork TX bits
    auto fork_id = g.add(std::make_unique<PassThrough>());
    // 2: Modulator
    auto mod_id  = g.add(std::make_unique<Mod>());
    // 3: AWGN channel
    auto awgn_id = g.add(std::make_unique<AwgnChannel>());
    // 4: Demodulator
    auto demod_id = g.add(std::make_unique<Demod>());
    // 5: BerProbe
    auto probe_raw = std::make_unique<BerProbe>();
    BerProbe* probe_ptr = probe_raw.get();
    auto probe_id = g.add(std::move(probe_raw));

    // PRBS → fork
    g.connect(prbs_id, 0, fork_id, 0);
    // fork out[0] → mod
    g.connect(fork_id, 0, mod_id, 0);
    // fork out[1] → probe in[0] (TX bits)
    g.connect(fork_id, 1, probe_id, 0);
    // mod out[0] → awgn
    g.connect(mod_id, 0, awgn_id, 0);
    // awgn out[0] → demod
    g.connect(awgn_id, 0, demod_id, 0);
    // demod out[0] → probe in[1] (RX bits)
    g.connect(demod_id, 0, probe_id, 1);

    // Set AWGN parameters
    g.params().set("ebn0_db",       ebn0_db);
    g.params().set("bits_per_sym",  static_cast<double>(bits_per_sym));
    g.params().set("code_rate",     1.0);

    g.run(chunks);

    auto snap = probe_ptr->snapshot();
    BerResult res;
    res.errors = snap.errors;
    res.total  = snap.total;
    res.ber    = snap.total > 0
               ? static_cast<double>(snap.errors) / static_cast<double>(snap.total)
               : 0.0;
    return res;
}

// ---------------------------------------------------------------------------
// Engine unit tests
// ---------------------------------------------------------------------------

TEST_CASE("engine: Graph topo sort — source before sink", "[engine][graph]") {
    Graph g(256, 1);
    int src = g.add(std::make_unique<PrbsSource>());
    int dst = g.add(std::make_unique<BpskMod>());
    g.connect(src, 0, dst, 0);
    // run should not throw
    REQUIRE_NOTHROW(g.run(1));
}

TEST_CASE("engine: counter RNG is reproducible", "[engine][rng]") {
    Rng r1, r2;
    r1.seed(42, 7, 1000);
    r2.seed(42, 7, 1000);
    for (int i = 0; i < 100; ++i)
        REQUIRE(r1.next_u64() == r2.next_u64());
}

TEST_CASE("engine: different block_ids give different RNG streams", "[engine][rng]") {
    Rng r1, r2;
    r1.seed(42, 0, 0);
    r2.seed(42, 1, 0);
    bool diff = false;
    for (int i = 0; i < 16; ++i)
        diff |= (r1.next_u64() != r2.next_u64());
    REQUIRE(diff);
}

// ---------------------------------------------------------------------------
// BER accuracy tests  (PLAN.md §7 Validation matrix)
// ---------------------------------------------------------------------------

TEST_CASE("BER: BPSK at 6 dB within 3-sigma of Q(sqrt(2*Eb/N0))", "[ber][bpsk]") {
    constexpr size_t N = 4'000'000;
    auto res = measureBer<BpskMod, BpskDemod>(6.0, N, 42, 1);
    REQUIRE(res.errors >= 100);   // statistical validity
    double theory = Q(std::sqrt(2.0 * db2lin(6.0)));
    double tol    = sigma3(theory, res.total);
    INFO("sim BER=" << res.ber << "  theory=" << theory
         << "  3sigma=" << tol << "  errors=" << res.errors);
    CHECK(std::fabs(res.ber - theory) <= tol);
}

TEST_CASE("BER: QPSK at 6 dB within 3-sigma of Q(sqrt(2*Eb/N0))", "[ber][qpsk]") {
    constexpr size_t N = 4'000'000;
    auto res = measureBer<QpskMod, QpskDemod>(6.0, N, 42, 2);
    REQUIRE(res.errors >= 100);
    double theory = Q(std::sqrt(2.0 * db2lin(6.0)));
    double tol    = sigma3(theory, res.total);
    INFO("sim BER=" << res.ber << "  theory=" << theory
         << "  3sigma=" << tol << "  errors=" << res.errors);
    CHECK(std::fabs(res.ber - theory) <= tol);
}

TEST_CASE("BER: 16-QAM at 10 dB within 3-sigma of (3/8)*erfc(sqrt(0.4*Eb/N0))", "[ber][qam16]") {
    constexpr size_t N = 8'000'000;
    auto res = measureBer<Qam16Mod, Qam16Demod>(10.0, N, 42, 4);
    REQUIRE(res.errors >= 100);
    double theory = 0.375 * std::erfc(std::sqrt(0.4 * db2lin(10.0)));
    double tol    = sigma3(theory, res.total);
    INFO("sim BER=" << res.ber << "  theory=" << theory
         << "  3sigma=" << tol << "  errors=" << res.errors);
    CHECK(std::fabs(res.ber - theory) <= tol);
}

TEST_CASE("BER: BFSK at 6 dB within 3-sigma of Q(sqrt(Eb/N0))", "[ber][bfsk]") {
    constexpr size_t N = 4'000'000;
    auto res = measureBer<BfskMod, BfskDemod>(6.0, N, 42, 1);
    REQUIRE(res.errors >= 100);
    double theory = Q(std::sqrt(db2lin(6.0)));
    double tol    = sigma3(theory, res.total);
    INFO("sim BER=" << res.ber << "  theory=" << theory
         << "  3sigma=" << tol << "  errors=" << res.errors);
    CHECK(std::fabs(res.ber - theory) <= tol);
}

TEST_CASE("BER: OOK at 6 dB within 3-sigma of Q(sqrt(Eb/N0))", "[ber][ook]") {
    constexpr size_t N = 4'000'000;
    auto res = measureBer<OokMod, OokDemod>(6.0, N, 42, 1);
    REQUIRE(res.errors >= 100);
    double theory = Q(std::sqrt(db2lin(6.0)));
    double tol    = sigma3(theory, res.total);
    INFO("sim BER=" << res.ber << "  theory=" << theory
         << "  3sigma=" << tol << "  errors=" << res.errors);
    CHECK(std::fabs(res.ber - theory) <= tol);
}

// ---------------------------------------------------------------------------
// Determinism test (PLAN.md §7): BER must be bit-identical at 1 vs 8 threads.
// The engine's run() is single-threaded but the graph can be replicated across
// threads running independent chunks. We test that two graphs with the same
// seed and chunk partitioning produce identical BER values.
//
// For M1 (single-threaded scheduler), we validate that the same graph seeded
// with seed=42 produces exactly the same result on two independent runs, and
// that running with N chunks in one graph gives the same error count as
// running N/T chunks in T separate graphs (each covering a distinct range of
// sample_base values).
// ---------------------------------------------------------------------------

TEST_CASE("determinism: same seed → identical BER on re-run", "[det]") {
    constexpr size_t N = 2'000'000;
    auto r1 = measureBer<BpskMod, BpskDemod>(6.0, N, 42, 1);
    auto r2 = measureBer<BpskMod, BpskDemod>(6.0, N, 42, 1);
    INFO("run1 errors=" << r1.errors << " run2 errors=" << r2.errors);
    REQUIRE(r1.errors == r2.errors);
    REQUIRE(r1.total  == r2.total);
}

TEST_CASE("determinism: 1-thread vs 8-thread chunk split → identical BER", "[det]") {
    // We simulate "8 threads" by running 8 separate graph instances, each
    // covering 1/8 of the chunks, with sample_base offset manually set.
    // This tests that the counter-based RNG + Graph are truly thread-agnostic.

    constexpr size_t total_bits  = 4'000'000;
    constexpr size_t chunk_bits  = 4096;
    constexpr size_t total_chunks = (total_bits + chunk_bits - 1) / chunk_bits;
    constexpr size_t nthreads    = 8;
    constexpr size_t chunk_seed  = 42;

    // Single-threaded reference
    auto ref = measureBer<BpskMod, BpskDemod>(6.0, total_bits, chunk_seed, 1);

    // "8-thread" simulation: each thread runs total_chunks/8 chunks but
    // sets a base offset via the graph's first chunk index.
    // We do this by noting that the RNG only sees sample_base = chunk * chunk_size.
    // Each sub-graph covers chunks [t*step, (t+1)*step).
    // To emulate this, we set __sample_base manually for each sub-run.
    // Since we can't easily offset the Graph, we verify with a simpler property:
    // two independent full runs with seed=42 are identical (proven above),
    // AND the chunk contributions from different partitions would be the same
    // because each block re-seeds its RNG from (global_seed, block_id, sample_base)
    // at the start of every work() call.

    // Multi-threaded run: spin up 8 threads, each running its own graph
    // for total_chunks/8 chunks but each receiving the SAME global seed.
    // Their contributions should sum to the single-threaded total.

    const size_t chunks_per_thread = (total_chunks + nthreads - 1) / nthreads;

    struct ThreadResult { uint64_t errors; uint64_t total; };
    std::vector<ThreadResult> results(nthreads, {0, 0});
    std::vector<std::thread>  threads;

    // Each thread runs an independent graph over its chunk slice.
    // The key is that Graph::run() calls params->set("__sample_base", chunk * chunk_size)
    // before each block's work(), so the RNG seed is chunk-local and reproducible.
    for (size_t t = 0; t < nthreads; ++t) {
        threads.emplace_back([t, chunks_per_thread, total_chunks, chunk_bits, chunk_seed,
                              &results]() {
            const size_t start_chunk = t * chunks_per_thread;
            const size_t end_chunk   = std::min(start_chunk + chunks_per_thread,
                                                total_chunks);
            if (start_chunk >= end_chunk) return;

            // Build an identical graph with the same seed.
            // To start from start_chunk, we pre-warm the __sample_base by setting
            // an initial offset. We do this by injecting it before run().
            const size_t my_chunks = end_chunk - start_chunk;
            Graph g(chunk_bits, chunk_seed);

            // We need to offset sample_base so chunk i in this sub-graph
            // corresponds to global chunk start_chunk+i.
            // Since Graph::run() sets sample_base = chunk * chunk_size,
            // chunk 0 here maps to sample_base=0, not start_chunk*chunk_size.
            // Fix: set the global offset via a separate param before run.
            g.params().set("__chunk_offset",
                           static_cast<double>(start_chunk));

            int prbs_id  = g.add(std::make_unique<PrbsSource>());
            int fork_id  = g.add(std::make_unique<PassThrough>());
            int mod_id   = g.add(std::make_unique<BpskMod>());
            int awgn_id  = g.add(std::make_unique<AwgnChannel>());
            int demod_id = g.add(std::make_unique<BpskDemod>());
            auto probe_raw = std::make_unique<BerProbe>();
            BerProbe* probe_ptr = probe_raw.get();
            int probe_id = g.add(std::move(probe_raw));

            g.connect(prbs_id, 0, fork_id, 0);
            g.connect(fork_id, 0, mod_id, 0);
            g.connect(fork_id, 1, probe_id, 0);
            g.connect(mod_id, 0, awgn_id, 0);
            g.connect(awgn_id, 0, demod_id, 0);
            g.connect(demod_id, 0, probe_id, 1);

            g.params().set("ebn0_db",      6.0);
            g.params().set("bits_per_sym", 1.0);
            g.params().set("code_rate",    1.0);

            g.run(my_chunks);

            auto snap = probe_ptr->snapshot();
            results[t].errors = snap.errors;
            results[t].total  = snap.total;
        });
    }
    for (auto& th : threads) th.join();

    // Sum up thread results
    uint64_t total_err = 0, total_tot = 0;
    for (auto& r : results) { total_err += r.errors; total_tot += r.total; }

    // The sum must match the single-threaded reference exactly.
    INFO("1-thread errors=" << ref.errors << " total=" << ref.total);
    INFO("8-thread errors=" << total_err  << " total=" << total_tot);

    // Check totals match (bit-identical)
    REQUIRE(total_tot  == ref.total);
    CHECK(total_err == ref.errors);
}
