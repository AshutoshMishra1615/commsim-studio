/**
 * cli/sweep.cpp  --  M5: Multithreaded coded-BER sweep CLI tool.
 *
 * Compares uncoded BPSK vs Hamming(7,4) vs Rate-1/2 K=7 Convolutional code
 * with hard and soft Viterbi decoding over Eb/N0 in [0, 7] dB.
 *
 * Outputs CSV and prints formatted console table.
 */
#include <cstdio>
#include <cmath>
#include <vector>
#include <string>
#include <thread>
#include <future>
#include <chrono>
#include <random>
#include <fstream>
#include <iostream>
#include <iomanip>

#include "blocks/coding/hamming74.hpp"
#include "blocks/coding/conv_k7.hpp"
#include "blocks/coding/viterbi.hpp"
#include "engine/rng.hpp"

enum class CodedScheme {
    UNCODED,
    HAMMING,
    CONV_HARD,
    CONV_SOFT
};

struct SweepResult {
    std::string scheme_name;
    double ebn0_db;
    uint64_t bits;
    uint64_t errors;
    double ber;
};

static double noiseSigma(double ebn0_db, double rate) {
    const double ebn0_lin = std::pow(10.0, ebn0_db / 10.0);
    const double N0 = 1.0 / (rate * ebn0_lin);
    return std::sqrt(N0 * 0.5);
}

static SweepResult runPoint(CodedScheme scheme, const std::string& name, double ebn0_db,
                            uint64_t max_bits, uint64_t min_errors, uint64_t seed) {
    double rate = 1.0;
    if (scheme == CodedScheme::HAMMING) rate = 4.0 / 7.0;
    else if (scheme == CodedScheme::CONV_HARD || scheme == CodedScheme::CONV_SOFT) rate = 0.5;

    const double sigma = noiseSigma(ebn0_db, rate);
    std::mt19937_64 rng(seed);
    std::normal_distribution<double> noise(0.0, sigma);

    uint64_t total_bits = 0;
    uint64_t bit_errors = 0;
    const size_t NINFO = 1000;

    while (total_bits < max_bits && (bit_errors < min_errors || total_bits < 50000)) {
        std::vector<uint8_t> info(NINFO);
        for (size_t i = 0; i < NINFO; ++i) {
            info[i] = static_cast<uint8_t>(rng() & 1u);
        }

        if (scheme == CodedScheme::UNCODED) {
            for (size_t i = 0; i < NINFO; ++i) {
                const double s = info[i] ? -1.0 : 1.0;
                const double r = s + noise(rng);
                const uint8_t dec = (r < 0.0) ? 1 : 0;
                if (dec != info[i]) bit_errors++;
            }
            total_bits += NINFO;
        } else if (scheme == CodedScheme::HAMMING) {
            // Encode in 4-bit nibbles
            const auto coded = blocks::Hamming74::encode(info);
            std::vector<uint8_t> rx(coded.size());
            for (size_t i = 0; i < coded.size(); ++i) {
                const double s = coded[i] ? -1.0 : 1.0;
                const double r = s + noise(rng);
                rx[i] = (r < 0.0) ? 1 : 0;
            }
            const auto decoded = blocks::Hamming74::decode(rx);
            for (size_t i = 0; i < NINFO && i < decoded.size(); ++i) {
                if (decoded[i] != info[i]) bit_errors++;
            }
            total_bits += NINFO;
        } else {
            const auto coded = blocks::ConvK7::encode(info);
            std::vector<double> rx(coded.size());
            for (size_t i = 0; i < coded.size(); ++i) {
                const double s = coded[i] ? -1.0 : 1.0;
                rx[i] = s + noise(rng);
            }
            const bool is_soft = (scheme == CodedScheme::CONV_SOFT);
            const auto decoded = blocks::Viterbi::decode(rx, NINFO, is_soft);
            for (size_t i = 0; i < NINFO && i < decoded.size(); ++i) {
                if (decoded[i] != info[i]) bit_errors++;
            }
            total_bits += NINFO;
        }
    }

    SweepResult res;
    res.scheme_name = name;
    res.ebn0_db = ebn0_db;
    res.bits = total_bits;
    res.errors = bit_errors;
    res.ber = total_bits > 0 ? (static_cast<double>(bit_errors) / static_cast<double>(total_bits)) : 0.0;
    return res;
}

int main(int argc, char** argv) {
    std::string csv_path = "results/coding_gain.csv";
    if (argc > 1) {
        csv_path = argv[1];
    }

    std::cout << "========================================================================\n";
    std::cout << " commsim-studio: Multithreaded Coded BER Sweep Tool (M5)\n";
    std::cout << " Schemes: Uncoded BPSK, Hamming(7,4), Conv K=7 Hard, Conv K=7 Soft\n";
    std::cout << "========================================================================\n\n";

    const double lo_db = 0.0;
    const double hi_db = 7.0;
    const double step_db = 0.5;
    std::vector<double> snr_points;
    for (double db = lo_db; db <= hi_db + 1e-6; db += step_db) {
        snr_points.push_back(db);
    }

    struct SchemeDesc {
        CodedScheme scheme;
        std::string name;
        uint64_t max_bits;
        uint64_t min_errors;
    };

    const std::vector<SchemeDesc> schemes = {
        { CodedScheme::UNCODED,   "Uncoded BPSK",        500000, 100 },
        { CodedScheme::HAMMING,   "Hamming(7,4) hard",   500000, 100 },
        { CodedScheme::CONV_HARD, "Conv K=7 R=1/2 hard", 600000,  80 },
        { CodedScheme::CONV_SOFT, "Conv K=7 R=1/2 soft", 600000,  80 }
    };

    std::vector<std::future<SweepResult>> futures;
    uint64_t task_id = 0;

    const auto t_start = std::chrono::steady_clock::now();

    for (const auto& s : schemes) {
        for (double db : snr_points) {
            uint64_t seed = 42u + 10007u * (++task_id);
            futures.push_back(std::async(std::launch::async, [=]() {
                return runPoint(s.scheme, s.name, db, s.max_bits, s.min_errors, seed);
            }));
        }
    }

    std::vector<SweepResult> results;
    results.reserve(futures.size());
    for (auto& f : futures) {
        results.push_back(f.get());
    }

    const auto t_end = std::chrono::steady_clock::now();
    const double elapsed_s = std::chrono::duration<double>(t_end - t_start).count();

    // Print console table
    std::cout << std::left
              << std::setw(24) << "Scheme"
              << std::setw(12) << "Eb/N0 (dB)"
              << std::setw(14) << "Bits"
              << std::setw(12) << "Errors"
              << std::setw(14) << "BER"
              << "\n";
    std::cout << std::string(76, '-') << "\n";

    for (const auto& r : results) {
        std::cout << std::left
                  << std::setw(24) << r.scheme_name
                  << std::setw(12) << std::fixed << std::setprecision(1) << r.ebn0_db
                  << std::setw(14) << r.bits
                  << std::setw(12) << r.errors
                  << std::setw(14) << std::scientific << std::setprecision(3) << r.ber
                  << "\n";
    }

    std::cout << "\nSweep completed in " << std::fixed << std::setprecision(2) << elapsed_s << " s.\n";

    // Write CSV
    std::ofstream out(csv_path);
    if (out.is_open()) {
        out << "scheme,ebn0_db,bits,errors,ber\n";
        for (const auto& r : results) {
            out << r.scheme_name << ","
                << std::fixed << std::setprecision(1) << r.ebn0_db << ","
                << r.bits << ","
                << r.errors << ","
                << std::scientific << std::setprecision(4) << r.ber << "\n";
        }
        std::cout << "Saved CSV results to " << csv_path << "\n";
    }

    return 0;
}
