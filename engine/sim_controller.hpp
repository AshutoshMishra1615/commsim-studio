#pragma once
/**
 * engine/sim_controller.hpp  --  M2-M4: Simulation controller and background thread.
 *
 * Implements PLAN.md §3 threading model:
 *   - Sim thread runs the graph in chunks of 4096 samples.
 *   - Probes copy data into lock-free triple buffers.
 *   - UI parameter changes go through an atomic/thread-safe command queue
 *     and are applied between chunks.
 *   - Target simulation rate pacing (default 1.0 Msym/s).
 *
 * M4 additions:
 *   - RRC pulse shaping & matched filtering
 *   - Dispersive multipath / ISI channel [1.0, 0.5, 0.3]
 *   - Timing-offset adjustment for symbol sampler & eye probe
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
#include "../blocks/channel/isi_fir.hpp"
#include "../blocks/mapping/bfsk.hpp"
#include "../blocks/mapping/bpsk.hpp"
#include "../blocks/mapping/ook.hpp"
#include "../blocks/mapping/psk8.hpp"
#include "../blocks/mapping/psk16.hpp"
#include "../blocks/mapping/qam16.hpp"
#include "../blocks/mapping/qam64.hpp"
#include "../blocks/mapping/qpsk.hpp"
#include "../blocks/probe/waveform_probe.hpp"
#include "../blocks/probe/constellation_probe.hpp"
#include "../blocks/probe/eye_probe.hpp"
#include "../blocks/probe/spectrum_probe.hpp"
#include "../blocks/shaping/rrc_filter.hpp"
#include "../blocks/shaping/matched_filter.hpp"
#include "../blocks/shaping/upsample.hpp"
#include "../blocks/shaping/downsample.hpp"
#include "../blocks/source/passthrough.hpp"
#include "../blocks/source/prbs.hpp"

namespace engine {

enum class Scheme {
    BPSK,
    QPSK,
    PSK8,
    QAM16,
    PSK16,
    QAM64,
    BFSK,
    OOK
};

inline const char* schemeToString(Scheme s) {
    switch (s) {
        case Scheme::BPSK:  return "BPSK";
        case Scheme::QPSK:  return "QPSK";
        case Scheme::PSK8:  return "8-PSK";
        case Scheme::QAM16: return "16-QAM";
        case Scheme::PSK16: return "16-PSK";
        case Scheme::QAM64: return "64-QAM";
        case Scheme::BFSK:  return "BFSK";
        case Scheme::OOK:   return "OOK";
    }
    return "Unknown";
}

inline Scheme stringToScheme(const std::string& str) {
    if (str == "QPSK") return Scheme::QPSK;
    if (str == "8-PSK" || str == "PSK8") return Scheme::PSK8;
    if (str == "16-QAM" || str == "QAM16") return Scheme::QAM16;
    if (str == "16-PSK" || str == "PSK16") return Scheme::PSK16;
    if (str == "64-QAM" || str == "QAM64") return Scheme::QAM64;
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
        ResetStats,
        SetShaping,
        SetIsi
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
        waveform_buffer_      = std::make_shared<TripleBuffer<blocks::WaveformSnapshot>>();
        constellation_buffer_ = std::make_shared<TripleBuffer<blocks::ConstellationSnapshot>>();
        eye_buffer_           = std::make_shared<TripleBuffer<blocks::EyeSnapshot>>();
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

    void setShapingEnabled(bool enabled) {
        Command cmd;
        cmd.type = Command::Type::SetShaping;
        cmd.value = enabled ? 1.0 : 0.0;
        cmd.timestamp = std::chrono::steady_clock::now();
        enqueueCommand(std::move(cmd));
    }

    void setIsiEnabled(bool enabled) {
        Command cmd;
        cmd.type = Command::Type::SetIsi;
        cmd.value = enabled ? 1.0 : 0.0;
        cmd.timestamp = std::chrono::steady_clock::now();
        enqueueCommand(std::move(cmd));
    }

    void setTimingOffset(float offset) {
        setParam("timing_offset", static_cast<double>(offset));
    }

    void setBeta(float beta) {
        setParam("beta", static_cast<double>(beta));
    }

    bool isShapingEnabled() const { return shaping_enabled_.load(std::memory_order_acquire); }
    bool isIsiEnabled() const { return isi_enabled_.load(std::memory_order_acquire); }
    float getBeta() const { return beta_.load(std::memory_order_acquire); }
    float getTimingOffset() const { return timing_offset_.load(std::memory_order_acquire); }

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

    std::shared_ptr<TripleBuffer<blocks::ConstellationSnapshot>> getConstellationBuffer() const {
        return constellation_buffer_;
    }

    std::shared_ptr<TripleBuffer<blocks::EyeSnapshot>> getEyeBuffer() const {
        return eye_buffer_;
    }

    blocks::SpectrumProbe::Snapshot getSpectrumSnapshot() const {
        if (spectrum_probe_) return spectrum_probe_->snapshot();
        return {};
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
                    } else if (cmd.key == "timing_offset") {
                        timing_offset_.store(static_cast<float>(cmd.value), std::memory_order_release);
                        if (downsample_) downsample_->setTimingOffset(static_cast<float>(cmd.value));
                        if (eye_probe_) eye_probe_->setTimingOffset(static_cast<float>(cmd.value));
                    } else if (cmd.key == "beta") {
                        beta_.store(static_cast<float>(cmd.value), std::memory_order_release);
                        buildGraph(current_scheme_);
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
                case Command::Type::SetShaping:
                    shaping_enabled_.store(cmd.value > 0.5, std::memory_order_release);
                    buildGraph(current_scheme_);
                    break;
                case Command::Type::SetIsi:
                    isi_enabled_.store(cmd.value > 0.5, std::memory_order_release);
                    if (isi_channel_) {
                        isi_channel_->setEnabled(cmd.value > 0.5);
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
                    if (constellation_probe_) constellation_probe_->reset();
                    if (eye_probe_) eye_probe_->reset();
                    break;
            }
        }
    }

    void buildGraph(Scheme scheme) {
        int bits_per_sym = 1;
        switch (scheme) {
            case Scheme::BPSK:  bits_per_sym = 1; break;
            case Scheme::QPSK:  bits_per_sym = 2; break;
            case Scheme::PSK8:  bits_per_sym = 3; break;
            case Scheme::QAM16: bits_per_sym = 4; break;
            case Scheme::PSK16: bits_per_sym = 4; break;
            case Scheme::QAM64: bits_per_sym = 6; break;
            case Scheme::BFSK:  bits_per_sym = 1; break;
            case Scheme::OOK:   bits_per_sym = 1; break;
        }

        const bool shaping = shaping_enabled_.load(std::memory_order_relaxed);
        const float beta   = beta_.load(std::memory_order_relaxed);
        const float toff   = timing_offset_.load(std::memory_order_relaxed);
        const bool isi_on  = isi_enabled_.load(std::memory_order_relaxed);

        if (shaping) {
            const size_t sps = 8;
            const size_t span = 12;
            const size_t total_filter_delay_sym = 2 * span; // 24 symbols
            const size_t csize = (chunk_size_ / (static_cast<size_t>(bits_per_sym) * sps)) * (static_cast<size_t>(bits_per_sym) * sps);
            const size_t sym_csize = csize / sps;
            auto g = std::make_unique<Graph>(sym_csize, seed_);

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
                case Scheme::PSK8:
                    mod_id   = g->add(std::make_unique<blocks::Psk8Mod>());
                    demod_id = g->add(std::make_unique<blocks::Psk8Demod>());
                    break;
                case Scheme::QAM16:
                    mod_id   = g->add(std::make_unique<blocks::Qam16Mod>());
                    demod_id = g->add(std::make_unique<blocks::Qam16Demod>());
                    break;
                case Scheme::PSK16:
                    mod_id   = g->add(std::make_unique<blocks::Psk16Mod>());
                    demod_id = g->add(std::make_unique<blocks::Psk16Demod>());
                    break;
                case Scheme::QAM64:
                    mod_id   = g->add(std::make_unique<blocks::Qam64Mod>());
                    demod_id = g->add(std::make_unique<blocks::Qam64Demod>());
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

            auto up_id     = g->add(std::make_unique<blocks::Upsample>(sps));
            auto tx_rrc_id = g->add(std::make_unique<blocks::RrcFilter>(sps, span, beta));
            auto isi_block = std::make_unique<blocks::IsiFir>(std::vector<float>{1.0f, 0.5f, 0.3f}, sps);
            isi_block->setEnabled(isi_on);
            isi_channel_   = isi_block.get();
            auto isi_id    = g->add(std::move(isi_block));

            auto awgn_id   = g->add(std::make_unique<blocks::AwgnChannel>());

            auto wf_block  = std::make_unique<blocks::WaveformProbe>(waveform_buffer_);
            waveform_probe_ = wf_block.get();
            auto wf_id     = g->add(std::move(wf_block));

            auto spec_block = std::make_unique<blocks::SpectrumProbe>(1024, 8.0 * 1e6);
            spectrum_probe_ = spec_block.get();
            auto spec_id    = g->add(std::move(spec_block));

            auto rx_mf_id  = g->add(std::make_unique<blocks::MatchedFilter>(sps, span, beta));

            auto eye_block = std::make_unique<blocks::EyeProbe>(eye_buffer_, 4096, static_cast<float>(sps));
            eye_block->setTimingOffset(toff);
            eye_probe_     = eye_block.get();
            auto eye_id    = g->add(std::move(eye_block));

            auto down_block = std::make_unique<blocks::Downsample>(sps, toff, 0);
            downsample_    = down_block.get();
            auto down_id   = g->add(std::move(down_block));

            auto constel_block = std::make_unique<blocks::ConstellationProbe>(constellation_buffer_);
            constellation_probe_ = constel_block.get();
            auto constel_id = g->add(std::move(constel_block));

            auto ber_block = std::make_unique<BerProbe>();
            ber_block->setDelay(total_filter_delay_sym * static_cast<size_t>(bits_per_sym));
            ber_probe_ = ber_block.get();
            auto probe_id = g->add(std::move(ber_block));

            // Connect graph:
            // prbs -> fork -> mod -> upsample -> tx_rrc -> isi -> awgn -> wf -> spec, rx_mf -> eye -> downsample -> constel -> demod -> probe
            g->connect(prbs_id, 0, fork_id, 0);
            g->connect(fork_id, 0, mod_id, 0);
            g->connect(fork_id, 1, probe_id, 0);    // TX reference bits
            g->connect(mod_id, 0, up_id, 0);
            g->connect(up_id, 0, tx_rrc_id, 0);
            g->connect(tx_rrc_id, 0, isi_id, 0);
            g->connect(isi_id, 0, awgn_id, 0);
            g->connect(awgn_id, 0, wf_id, 0);       // Continuous waveform probe
            g->connect(wf_id, 0, spec_id, 0);       // Spectrum probe
            g->connect(spec_id, 0, rx_mf_id, 0);    // Forward to matched filter
            g->connect(rx_mf_id, 0, eye_id, 0);     // Eye probe after matched filter
            g->connect(eye_id, 0, down_id, 0);      // Downsample at decision instant
            g->connect(down_id, 0, constel_id, 0);  // Constellation of sampled symbols
            g->connect(constel_id, 0, demod_id, 0);
            g->connect(demod_id, 0, probe_id, 1);   // RX decoded bits

            g->params().set("ebn0_db", ebn0_db_.load(std::memory_order_relaxed));
            g->params().set("bits_per_sym", static_cast<double>(bits_per_sym));
            g->params().set("code_rate", 1.0);

            graph_ = std::move(g);
        } else {
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
                case Scheme::PSK8:
                    mod_id   = g->add(std::make_unique<blocks::Psk8Mod>());
                    demod_id = g->add(std::make_unique<blocks::Psk8Demod>());
                    break;
                case Scheme::QAM16:
                    mod_id   = g->add(std::make_unique<blocks::Qam16Mod>());
                    demod_id = g->add(std::make_unique<blocks::Qam16Demod>());
                    break;
                case Scheme::PSK16:
                    mod_id   = g->add(std::make_unique<blocks::Psk16Mod>());
                    demod_id = g->add(std::make_unique<blocks::Psk16Demod>());
                    break;
                case Scheme::QAM64:
                    mod_id   = g->add(std::make_unique<blocks::Qam64Mod>());
                    demod_id = g->add(std::make_unique<blocks::Qam64Demod>());
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

            auto isi_block = std::make_unique<blocks::IsiFir>(std::vector<float>{1.0f, 0.5f, 0.3f}, 1);
            isi_block->setEnabled(isi_on);
            isi_channel_ = isi_block.get();
            auto isi_id = g->add(std::move(isi_block));

            auto awgn_id = g->add(std::make_unique<blocks::AwgnChannel>());

            auto wf_block = std::make_unique<blocks::WaveformProbe>(waveform_buffer_);
            waveform_probe_ = wf_block.get();
            auto wf_id = g->add(std::move(wf_block));

            auto spec_block = std::make_unique<blocks::SpectrumProbe>(1024, 1.0 * 1e6);
            spectrum_probe_ = spec_block.get();
            auto spec_id    = g->add(std::move(spec_block));

            auto constel_block = std::make_unique<blocks::ConstellationProbe>(constellation_buffer_);
            constellation_probe_ = constel_block.get();
            auto constel_id = g->add(std::move(constel_block));

            auto eye_block = std::make_unique<blocks::EyeProbe>(eye_buffer_);
            eye_probe_ = eye_block.get();
            auto eye_id = g->add(std::move(eye_block));

            auto ber_block = std::make_unique<BerProbe>();
            ber_probe_ = ber_block.get();
            auto probe_id = g->add(std::move(ber_block));

            downsample_ = nullptr;

            // Connect graph
            g->connect(prbs_id, 0, fork_id, 0);
            g->connect(fork_id, 0, mod_id, 0);
            g->connect(fork_id, 1, probe_id, 0);   // TX bits
            g->connect(mod_id, 0, isi_id, 0);
            g->connect(isi_id, 0, awgn_id, 0);
            g->connect(awgn_id, 0, wf_id, 0);       // Tap noisy symbols to waveform probe
            g->connect(wf_id, 0, spec_id, 0);      // Tap to spectrum probe
            g->connect(spec_id, 0, constel_id, 0); // Forward to constellation probe
            g->connect(constel_id, 0, eye_id, 0);  // Tap to eye probe
            g->connect(eye_id, 0, demod_id, 0);
            g->connect(demod_id, 0, probe_id, 1);  // RX bits

            // Apply parameters
            g->params().set("ebn0_db", ebn0_db_.load(std::memory_order_relaxed));
            g->params().set("bits_per_sym", static_cast<double>(bits_per_sym));
            g->params().set("code_rate", 1.0);

            graph_ = std::move(g);
        }
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
    blocks::ConstellationProbe* constellation_probe_ = nullptr;
    blocks::EyeProbe* eye_probe_ = nullptr;
    blocks::IsiFir* isi_channel_ = nullptr;
    blocks::Downsample* downsample_ = nullptr;
    blocks::SpectrumProbe* spectrum_probe_ = nullptr;
    std::shared_ptr<TripleBuffer<blocks::WaveformSnapshot>> waveform_buffer_;
    std::shared_ptr<TripleBuffer<blocks::ConstellationSnapshot>> constellation_buffer_;
    std::shared_ptr<TripleBuffer<blocks::EyeSnapshot>> eye_buffer_;

    Scheme current_scheme_ = Scheme::BPSK;
    std::atomic<double> ebn0_db_{6.0};
    std::atomic<bool> paused_{false};
    std::atomic<bool> running_{false};
    std::atomic<double> target_rate_msym_s_{1.0};
    std::atomic<double> measured_sim_rate_{0.0};
    std::atomic<double> last_latency_ms_{0.0};

    std::atomic<bool> shaping_enabled_{false};
    std::atomic<bool> isi_enabled_{false};
    std::atomic<float> beta_{0.35f};
    std::atomic<float> timing_offset_{0.0f};

    std::mutex cmd_mutex_;
    std::condition_variable cv_;
    std::deque<Command> commands_;

    std::thread sim_thread_;
};

}  // namespace engine
