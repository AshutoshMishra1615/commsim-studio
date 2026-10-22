# commsim Studio: Execution Plan

Simulation + visualization engine for the Data Communications course.
Language: C++17. Build: CMake. Executor: Antigravity. Rules: see `AGENTS.md`.

## 1. Decision: own engine?
**Build** the simulation engine and a small GPU density renderer. **Reuse** everything generic. **Do not build** a full render engine.

| Layer | Decision | Reason |
|---|---|---|
| Signal-graph sim engine (blocks, streams, scheduler, deterministic RNG) | Build | Core of the project; reusable blocks, multithreaded sweeps, reproducible runs |
| GPU density renderer (constellation persistence, eye heat map) | Build | About 400 lines of OpenGL; real-time accumulation of 1M+ points is what generic plot widgets do badly |
| Window, input, fonts, widgets, line plots | Reuse (SDL2, Dear ImGui, ImPlot) | A UI toolkit costs 3+ weeks and adds no course credit |
| FFT | Reuse (pocketfft) | Correctness-critical, solved problem |
| Game-style render engine (scene graph, ECS) | Do not build | Not needed; biggest schedule risk |

Rule: anything in the syllabus (modulation, coding, ISI, multiple access, PCM) is written by us. Generic infrastructure is a dependency.

## 2. Goals, scope, non-goals
Goals:
1. Interactive desktop app: pick a scheme, move an Eb/N0 slider, see constellation, eye, spectrum, waveform and live BER at 60 FPS.
2. Headless batch mode: reproducible BER and coding-gain sweeps to CSV, multithreaded.
3. Every result validated against closed-form theory with stated tolerances (section 7).
4. Full syllabus coverage (section 8) and a 6-8 page report.

Non-goals: real RF hardware, SDR drivers, an actual network stack, mobile/web build, GPU compute for the DSP.

Starting point (reuse as M0): header-only prototype in `legacy/commsim/` with BPSK, QPSK, 16-QAM, BFSK, OOK, AWGN, CRC-16, Hamming(7,4), K=7 convolutional code with soft/hard Viterbi, PCM, delta modulation, Walsh/PN CDMA, raised-cosine eye data, selftest (9 tests passing), CSV output and Python plots. This plan refactors it into the block-graph engine.

## 3. Architecture
Layers, top to bottom:
- App (viewer: ImGui + ImPlot + GL) and CLI (batch sweeps to CSV)
- Engine: Graph, Scheduler, Stream buffers, Probes, RNG
- Blocks (DSP): sources, mappers, pulse shaping, channels, coders, mux/MAC, protocol
- Third party: SDL2, Dear ImGui, ImPlot, pocketfft, Catch2

Threading (viewer): sim thread runs the graph in chunks of 4096 samples. Probes copy data into a lock-free triple buffer. The render thread reads only the latest snapshot. UI parameter changes go through an atomic command queue and are applied between chunks.

Target engine API:
```cpp
using cf = std::complex<float>;
template<class T> struct Stream { std::vector<T> buf; };      // one chunk
struct Block {
  virtual ~Block() = default;
  virtual const char* name() const = 0;
  virtual void work(const std::vector<Stream<cf>*>& in,
                    std::vector<Stream<cf>*>& out, size_t n) = 0;
  virtual void setParam(const std::string& key, double v) {}  // UI/CLI control
};
struct Graph {
  int  add(std::unique_ptr<Block>);
  void connect(int src, int srcPort, int dst, int dstPort);
  void run(size_t chunks);                                    // topological order
};
```
Determinism: counter-based RNG (Philox or PCG) keyed by (seed, block id, sample index).
Numeric policy: `complex<float>` streams, `double` for accumulators and theory. Errors counted on bits only.

## 4. Tech stack
| Item | Choice |
|---|---|
| Language / build | C++17, CMake >= 3.20, FetchContent for all deps, Ninja |
| Window / GL | SDL2, OpenGL 3.3 core, glad |
| UI / plots | Dear ImGui (docking branch), ImPlot |
| FFT | pocketfft (header-only) |
| Tests | Catch2 v3 |
| Report plots | Python + Matplotlib, reading engine CSV |
| Optional | miniaudio (acoustic modem), OpenMP (sweeps) |
| Platforms | Windows 10/11 primary, Linux secondary. CI: GitHub Actions build + tests |

## 5. Repository layout
```
commsim-studio/
  CMakeLists.txt  PLAN.md  README.md  AGENTS.md
  legacy/commsim/            (read-only prototype)
  engine/   graph.hpp scheduler.cpp stream.hpp rng.hpp probe.hpp params.hpp
  blocks/
    source/ prbs text_source sine_source
    mapping/ bpsk qpsk qam16 mary_psk mary_qam fsk ask
    shaping/ rc_filter rrc_filter upsample downsample matched_filter
    channel/ awgn isi_fir phase_cfo rayleigh(stretch)
    coding/ crc16 hamming74 conv_k7 viterbi interleaver
    source_coding/ pcm quantizer delta_mod
    mac/ fdma tdma cdma_walsh
    protocol/ framer arq_stopwait arq_gbn encapsulation_demo
    analog/ am fm pm
    probe/ ber_probe constellation_probe eye_probe spectrum_probe
  render/   gl_context.cpp density_renderer.cpp shaders/
  app/      main.cpp panels/ (constellation eye spectrum waveform ber coding mac protocol)
  cli/      sweep.cpp
  tests/    one file per block + theory_validation.cpp
  scripts/  plot_report.py
  docs/     report/ screenshots/ demo_script.md
```

## 6. Milestones
One person-week is about 12 h. Total about 67 h over 5 weeks. Do not start a milestone until the previous one's acceptance numbers are met.

### M0 (3 h): Project skeleton
Tasks: import prototype from `legacy/commsim` as a static library target, CMake + FetchContent, Catch2, CI, empty SDL2 + ImGui window.
Acceptance: clean build on Windows and Linux; 9 legacy tests pass.

### M1 (8 h): Engine core
Tasks: Block, Stream, Graph, topological scheduler, counter-based RNG, ParamStore, ber_probe. Port BPSK, QPSK, 16-QAM, BFSK, OOK and AWGN as blocks.
Acceptance: BPSK BER at 6 dB within 3 sigma of Q(sqrt(2Eb/N0)); output bit-identical at 1 and 8 threads.

### M2 (10 h): Viewer v1
Tasks: SDL2 + ImGui app, sim thread, triple buffer, controls (scheme, Eb/N0, run/pause), ImPlot BER readout, waveform panel.
Acceptance: 60 FPS at 1 Msym/s sim rate; slider change reflected in under 100 ms.

### M3 (8 h): GPU density renderer
Tasks: point-sprite accumulation into a float FBO with decay, colour map; constellation and eye panels.
Acceptance: 1M points/s accumulated at 60 FPS; 16-QAM clusters clearly visible at 10 dB.

### M4 (10 h): Pulse shaping and ISI
Tasks: RC/RRC filter, up/downsampling, matched filter, ISI FIR channel, eye probe, timing-offset slider.
Acceptance: zero ISI at symbol instants (max error < 1e-3 for beta = 0.35); eye visibly closes with taps [1, 0.5, 0.3].

### M5 (8 h): Coding blocks
Tasks: CRC-16, Hamming(7,4), K=7 Viterbi (soft LLR and hard), block interleaver; coded-BER sweep in CLI and coding panel.
Acceptance: soft Viterbi reaches BER 1e-5 at <= 4.3 dB Eb/N0; hard at <= 6.6 dB; Hamming corrects 100% of single errors.

### M6 (8 h): Source coding, analog, M-ary
Tasks: PCM and DM blocks, AM/FM/PM modulators with spectrum probe, M-ary PSK (8, 16), 64-QAM, capacity panel (Shannon curve with each scheme's operating point).
Acceptance: PCM SNR within 0.5 dB of 6.02N + 1.76; FM bandwidth matches Carson's rule within 10%; M-PSK/QAM BER within tolerance of theory.

### M7 (6 h): Multiple access and protocol
Tasks: FDMA, TDMA, CDMA (Walsh, PN); framer, stop-and-wait and go-back-N ARQ; OSI / TCP-IP encapsulation view (app data -> TCP -> IP -> Ethernet frame with real header fields and CRC).
Acceptance: CDMA Walsh shows no multi-user interference at 12 users; PN BER rises monotonically with users; stop-and-wait efficiency matches (1 - Pf)/(1 + 2a) within 5%.

### M8 (6 h): Polish and report
Tasks: screenshot tour, demo script, README, report, release build.
Acceptance: clean-clone build in under 5 min; all tests green; report PDF complete.

Stretch (only after M8): Gardner timing recovery and Costas loop, OFDM block, Rayleigh fading channel, acoustic modem via miniaudio.

## 7. Validation matrix (enforced by tests)
Tolerance: simulated BER within +/-3 sigma binomial error of theory, with at least 100 errors per point.

| Test | Theory | Points |
|---|---|---|
| BPSK / QPSK BER | Q(sqrt(2Eb/N0)) | 0-10 dB, step 1 |
| 16-QAM BER (Gray) | (3/8) erfc(sqrt(0.4 Eb/N0)) | 0-12 dB |
| Coherent BFSK, OOK | Q(sqrt(Eb/N0)) | 0-12 dB |
| 8-PSK | approx (2/log2 M) Q(sqrt(2 log2(M) Eb/N0) sin(pi/M)) | 4-14 dB |
| Raised-cosine ISI | h(kT) = delta_k | beta in {0.25, 0.35, 0.5, 1} |
| Conv code coding gain | >= 4.5 dB soft, >= 2.8 dB hard at BER 1e-5 | 0-7 dB |
| PCM SNR | 6.02N + 1.76 dB | N = 2..12 |
| CRC-16 | detects all bursts <= 16 bits | exhaustive for 64-bit message |
| Walsh orthogonality | <w_i, w_j> = N delta_ij | SF 8, 16, 64 |
| Determinism | identical BER CSV for 1 vs 8 threads | seed 42 |

## 8. Syllabus coverage
| Topic | Where |
|---|---|
| Time/frequency representation, signals | waveform + spectrum panels (M2, M6) |
| Information rate, channel capacity, bandwidth efficiency | capacity panel (M6) |
| Baseband transmission, ISI, Nyquist, eye, BER | M4 |
| Error detection/correction, block and convolutional coding | M5 (coded modulation as stretch) |
| AM, FM, PM | M6 |
| ASK, FSK, PSK, M-ary, QAM, MODEM | M1, M6 |
| PCM, DM, source coding | M6 (Huffman block if time, 2 h) |
| FDMA, TDMA, CDMA, combined | M7 (combined = TDMA frames of CDMA users) |
| Standards, protocols, OSI, TCP/IP | M7 encapsulation view |
| Historical overview, link elements, terms | report chapter 1 (no code) |

## 9. Performance targets (single core, release build)
| Metric | Target |
|---|---|
| Uncoded BPSK/QPSK headless | >= 20 Mbit/s |
| 16-QAM headless | >= 15 Mbit/s |
| Soft Viterbi K=7 | >= 1.0 Mbit/s info |
| Viewer frame rate, all panels open | 60 FPS |
| Parameter-change latency | < 100 ms |
| Sweep scaling, 8 threads | >= 6x over 1 thread |

## 10. Schedule
| Week | Milestones | Checkpoint |
|---|---|---|
| 1 | M0, M1 | headless BER matches theory |
| 2 | M2, M3 | live constellation at 60 FPS |
| 3 | M4, M5 | eye diagram + coding gain plots |
| 4 | M6, M7 | all syllabus topics covered |
| 5 | M8 + buffer | report, demo, release |

If a milestone slips by more than 3 days, cut its stretch items, never its acceptance tests. Cut order if time runs out: Huffman, combined multiple access, go-back-N ARQ, FM/PM (keep AM).

## 11. Risks
| Risk | Impact | Mitigation |
|---|---|---|
| Agent scope creep (rewrites UI, adds deps) | High | AGENTS.md rules, one milestone per task, diff review |
| OpenGL/driver issues on lab machines | Medium | CPU density renderer fallback (ImPlot scatter) behind a flag |
| Threading bugs in viewer | Medium | triple buffer only, no shared mutable state, TSAN run in CI |
| Unreproducible BER differences | Medium | counter-based RNG, determinism test in CI |
| Windows build friction with SDL2 | Low | FetchContent pinned versions, prebuilt release zip |

## 12. Definition of done
- All milestones M0-M8 accepted with measured numbers logged.
- Release build runs the viewer and the `sweep` CLI on a clean machine.
- Report (6-8 pages): theory recap, architecture, validation table, 8+ figures, performance table, syllabus mapping.
- 3-minute demo: BPSK -> QPSK -> 16-QAM constellation, add ISI and close the eye, enable the convolutional code and show the BER cliff, CDMA with 8 users.

## 13. How to run in Antigravity
First message:
```
Read PLAN.md and AGENTS.md. Execute milestone M0 only. Create the CMake project,
import the legacy prototype from legacy/commsim as a static library target, add
Catch2 and an empty SDL2+ImGui window. Run the build and the 9 legacy tests, then
report results. Do not start M1.
```
Per-milestone template:
```
Execute milestone M<k> from PLAN.md section 6. Follow the task list and meet every
acceptance number. Write tests first where possible. When finished, paste the
measured acceptance numbers and stop.
```
Review checklist (about 10 min per milestone): clean-clone build works; acceptance numbers in the log match section 6; no new dependencies; screenshot saved to `docs/screenshots/`; commit before the next milestone.
