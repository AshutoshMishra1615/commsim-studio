#pragma once
/**
 * engine/sim_controller.hpp  --  M2: Simulation controller and background thread.
 *
 * Implements PLAN.md §3 threading model:
 *   - Sim thread runs the graph in chunks of 4096 samples.
 *   - Probes copy data into lock-free triple buffers.
 *   - UI parameter changes go through an atomic/thread-safe command queue
 *     and are applied between chunks.
 *   - Target simulation rate pacing (default 1.0 Msym/s).
 */
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "graph.hpp"
#include "probe.hpp"
#include "triple_buffer.hpp"
#include "../blocks/channel/awgn.hpp"
#include "../blocks/mapping/bfsk.hpp"
#include "../blocks/mapping/bpsk.hpp"
#include "../blocks/mapping/ook.hpp"
#include "../blocks/mapping/qam16.hpp"
#include "../blocks/mapping/qpsk.hpp"
#include "../blocks/probe/waveform_probe.hpp"
#include "../blocks/source/passthrough.hpp"
#include "../blocks/source/prbs.hpp"

namespace engine {

enum class Scheme {
    BPSK,
    QPSK,
    QAM16,
    BFSK,
    OOK
};

inline const char* schemeToString(Scheme s) {
    switch (s) {
        case Scheme::BPSK:  return "BPSK";
        case Scheme::QPSK:  return "QPSK";
        case Scheme::QAM16: return "16-QAM";
        case Scheme::BFSK:  return "BFSK";
        case Scheme::OOK:   return "OOK";
    }
    return "Unknown";
}

inline Scheme stringToScheme(const std::string& str) {
    if (str == "QPSK") return Scheme::QPSK;
    if (str == "16-QAM" || str == "QAM16") return Scheme::QAM16;
    if (str == "BFSK") return Scheme::BFSK;
    if (str == "OOK") return Scheme::OOK;
    return Scheme::BPSK;
}

struct Command {
    enum class Type {
        SetParam,
        SetScheme,
        Pause,
        Resume,
        ResetStats
    } type;
    std::string key;
    double value = 0.0;
    Scheme scheme = Scheme::BPSK;
    std::chrono::steady_clock::time_point timestamp;
};

class SimController {
public:
    explicit SimController(size_t chunk_size = 4096, uint64_t seed = 42)
        : chunk_size_(chunk_size), seed_(seed) {
        waveform_buffer_ = std::make_shared<TripleBuffer<blocks::WaveformSnapshot>>();
        buildGraph(current_scheme_);
    }

    ~SimController() {
        stop();
    }

    void start() {
        if (running_.exchange(true)) return;
        sim_thread_ = std::thread(&SimController::runLoop, this);
    }

    void stop() {
        if (!running_.exchange(false)) return;
        cv_.notify_all();
        if (sim_thread_.joinable()) {
            sim_thread_.join();
        }
    }

    void setParam(const std::string& key, double val) {
        Command cmd;
        cmd.type = Command::Type::SetParam;
        cmd.key = key;
        cmd.value = val;
        cmd.timestamp = std::chrono::steady_clock::now();
        enqueueCommand(std::move(cmd));
    }

    void setScheme(Scheme s) {
        Command cmd;
        cmd.type = Command::Type::SetScheme;
        cmd.scheme = s;
        cmd.timestamp = std::chrono::steady_clock::now();
        enqueueCommand(std::move(cmd));
    }

    void setPause(bool paused) {
        Command cmd;
        cmd.type = paused ? Command::Type::Pause : Command::Type::Resume;
        cmd.timestamp = std::chrono::steady_clock::now();
        enqueueCommand(std::move(cmd));
    }

    void resetStats() {
        Command cmd;
        cmd.type = Command::Type::ResetStats;
        cmd.timestamp = std::chrono::steady_clock::now();
        enqueueCommand(std::move(cmd));
    }

    /// Set target simulation rate in Msym/s (0 = unthrottled)
    void setTargetRate(double rate_msym_s) {
        target_rate_msym_s_.store(rate_msym_s, std::memory_order_release);
    }

    double getTargetRate() const {
        return target_rate_msym_s_.load(std::memory_order_acquire);
    }

    double getEbN0() const {
        return ebn0_db_.load(std::memory_order_acquire);
    }

    bool isPaused() const {
        return paused_.load(std::memory_order_acquire);
    }

    Scheme getScheme() const {
        return current_scheme_;
    }

    double getSimRateMsymS() const {
        return measured_sim_rate_.load(std::memory_order_acquire);
    }

    double getLastLatencyMs() const {
        return last_latency_ms_.load(std::memory_order_acquire);
    }

    BerProbe::Snapshot getBerSnapshot() const {
        if (ber_probe_) return ber_probe_->snapshot();
        return {};
    }

    std::shared_ptr<TripleBuffer<blocks::WaveformSnapshot>> getWaveformBuffer() const {
        return waveform_buffer_;
    }

    /// Run N chunks synchronously
    void runSynchronous(size_t chunks) {
        processCommands();
        if (graph_) {
            graph_->run(chunks);
        }
    }

private:
    void enqueueCommand(Command cmd) {
        {
            std::lock_guard<std::mutex> lock(cmd_mutex_);
            commands_.push_back(std::move(cmd));
        }
        cv_.notify_one();
    }

    void processCommands() {
        std::vector<Command> cmds;
        {
            std::lock_guard<std::mutex> lock(cmd_mutex_);
            cmds.assign(commands_.begin(), commands_.end());
            commands_.clear();
        }

        const auto now = std::chrono::steady_clock::now();
        for (const auto& cmd : cmds) {
            const double latency = std::chrono::duration<double, std::milli>(now - cmd.timestamp).count();
            last_latency_ms_.store(latency, std::memory_order_release);

            switch (cmd.type) {
                case Command::Type::SetParam:
                    if (cmd.key == "ebn0_db") {
                        ebn0_db_.store(cmd.value, std::memory_order_release);
                    }
                    if (graph_) {
                        graph_->params().set(cmd.key, cmd.value);
                    }
                    break;
                case Command::Type::SetScheme:
                    if (cmd.scheme != current_scheme_) {
                        current_scheme_ = cmd.scheme;
                        buildGraph(current_scheme_);
                    }
                    break;
                case Command::Type::Pause:
                    paused_.store(true, std::memory_order_release);
                    break;
                case Command::Type::Resume:
                    paused_.store(false, std::memory_order_release);
                    break;
                case Command::Type::ResetStats:
                    if (ber_probe_) ber_probe_->reset();
                    if (waveform_probe_) waveform_probe_->reset();
                    break;
            }
        }
    }

    void buildGraph(Scheme scheme) {
        int bits_per_sym = 1;
        switch (scheme) {
            case Scheme::BPSK:  bits_per_sym = 1; break;
            case Scheme::QPSK:  bits_per_sym = 2; break;
            case Scheme::QAM16: bits_per_sym = 4; break;
            case Scheme::BFSK:  bits_per_sym = 1; break;
            case Scheme::OOK:   bits_per_sym = 1; break;
        }

        const size_t csize = (chunk_size_ / static_cast<size_t>(bits_per_sym)) * static_cast<size_t>(bits_per_sym);
        auto g = std::make_unique<Graph>(csize, seed_);

        auto prbs_id = g->add(std::make_unique<blocks::PrbsSource>());
        auto fork_id = g->add(std::make_unique<blocks::PassThrough>());

        int mod_id = -1;
        int demod_id = -1;
        switch (scheme) {
            case Scheme::BPSK:
                mod_id   = g->add(std::make_unique<blocks::BpskMod>());
                demod_id = g->add(std::make_unique<blocks::BpskDemod>());
                break;
            case Scheme::QPSK:
                mod_id   = g->add(std::make_unique<blocks::QpskMod>());
                demod_id = g->add(std::make_unique<blocks::QpskDemod>());
                break;
            case Scheme::QAM16:
                mod_id   = g->add(std::make_unique<blocks::Qam16Mod>());
                demod_id = g->add(std::make_unique<blocks::Qam16Demod>());
                break;
            case Scheme::BFSK:
                mod_id   = g->add(std::make_unique<blocks::BfskMod>());
                demod_id = g->add(std::make_unique<blocks::BfskDemod>());
                break;
            case Scheme::OOK:
                mod_id   = g->add(std::make_unique<blocks::OokMod>());
                demod_id = g->add(std::make_unique<blocks::OokDemod>());
                break;
        }

        auto awgn_id = g->add(std::make_unique<blocks::AwgnChannel>());

        auto wf_block = std::make_unique<blocks::WaveformProbe>(waveform_buffer_);
        waveform_probe_ = wf_block.get();
        auto wf_id = g->add(std::move(wf_block));

        auto ber_block = std::make_unique<BerProbe>();
        ber_probe_ = ber_block.get();
        auto probe_id = g->add(std::move(ber_block));

        // Connect graph
        g->connect(prbs_id, 0, fork_id, 0);
        g->connect(fork_id, 0, mod_id, 0);
        g->connect(fork_id, 1, probe_id, 0);   // TX bits
        g->connect(mod_id, 0, awgn_id, 0);
        g->connect(awgn_id, 0, wf_id, 0);       // Tap noisy symbols to waveform probe
        g->connect(wf_id, 0, demod_id, 0);
        g->connect(demod_id, 0, probe_id, 1);  // RX bits

        // Apply parameters
        g->params().set("ebn0_db", ebn0_db_.load(std::memory_order_relaxed));
        g->params().set("bits_per_sym", static_cast<double>(bits_per_sym));
        g->params().set("code_rate", 1.0);

        graph_ = std::move(g);
    }

    void runLoop() {
        auto last_rate_calc = std::chrono::steady_clock::now();
        uint64_t symbols_since_calc = 0;

        while (running_.load(std::memory_order_acquire)) {
            processCommands();

            if (paused_.load(std::memory_order_acquire)) {
                std::unique_lock<std::mutex> lock(cmd_mutex_);
                cv_.wait_for(lock, std::chrono::milliseconds(5), [this] {
                    return !running_ || !commands_.empty() || !paused_;
                });
                continue;
            }

            const auto t_start = std::chrono::steady_clock::now();
            if (graph_) {
                graph_->run(1);
            }
            const auto t_end = std::chrono::steady_clock::now();

            symbols_since_calc += chunk_size_;

            // Calculate rate every ~100 ms
            const auto elapsed_sec = std::chrono::duration<double>(t_end - last_rate_calc).count();
            if (elapsed_sec >= 0.1) {
                const double rate = (static_cast<double>(symbols_since_calc) / elapsed_sec) / 1e6;
                measured_sim_rate_.store(rate, std::memory_order_release);
                symbols_since_calc = 0;
                last_rate_calc = t_end;
            }

            // Pacing if target rate is specified (> 0)
            const double target_rate = target_rate_msym_s_.load(std::memory_order_relaxed);
            if (target_rate > 0.0) {
                const double target_chunk_sec = (static_cast<double>(chunk_size_) / (target_rate * 1e6));
                const auto chunk_elapsed_sec = std::chrono::duration<double>(t_end - t_start).count();
                if (chunk_elapsed_sec < target_chunk_sec) {
                    const auto sleep_dur = std::chrono::duration<double>(target_chunk_sec - chunk_elapsed_sec);
                    std::this_thread::sleep_for(sleep_dur);
                }
            }
        }
    }

    size_t chunk_size_;
    uint64_t seed_;

    std::unique_ptr<Graph> graph_;
    BerProbe* ber_probe_ = nullptr;
    blocks::WaveformProbe* waveform_probe_ = nullptr;
    std::shared_ptr<TripleBuffer<blocks::WaveformSnapshot>> waveform_buffer_;

    Scheme current_scheme_ = Scheme::BPSK;
    std::atomic<double> ebn0_db_{6.0};
    std::atomic<bool> paused_{false};
    std::atomic<bool> running_{false};
    std::atomic<double> target_rate_msym_s_{1.0};
    std::atomic<double> measured_sim_rate_{0.0};
    std::atomic<double> last_latency_ms_{0.0};

    std::mutex cmd_mutex_;
    std::condition_variable cv_;
    std::deque<Command> commands_;

    std::thread sim_thread_;
};

}  // namespace engine
