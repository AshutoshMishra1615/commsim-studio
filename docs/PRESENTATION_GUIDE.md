# commsim-studio — Presentation Guide
**Course: Data Communications and Networks**

**Team:**
- Ashutosh Mishra (124EI0048)
- Tamanna Sahu (124EI0008)
- Sabyasachi Karsharma (124EI0032)
- Aditya Ankush Dhonde (124EI0029)

---

## Overview

This guide explains how to present **commsim-studio** to your professor in a clear, structured, and confident way. The session should take **15–20 minutes**. Each section below maps to a tab or feature of the software and links to the theory.

---

## Before You Start

### Build & launch (run once before the session)

```bash
cd /home/pyro/Projects/commsim-studio

# 1. Build (should take < 60 s on a clean machine)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

# 2. Run all 62 tests — show the professor this output
ctest --test-dir build --output-on-failure

# 3. Launch the interactive viewer
DISPLAY=:0 ./build/app/commsim_studio
```

> [!TIP]
> Run ctest **before** your presentation so the green pass output is on screen. This immediately shows the project is verified against theory.

### Window setup
- Resize the window to **1920×1080** (press **F11** for fullscreen).
- Keep the **left sidebar** visible — it shows Eb/N0, BER, FPS live.
- Use **Ctrl+1 through Ctrl+7** to switch tabs instantly during the demo.

---

## Presentation Script

### 0. Introduction (1 min)

> "We built **commsim-studio**, a real-time C++17 simulation and visualization engine for the core topics of Data Communications. The simulator runs on a background thread, processing over **1 million symbols per second**, while the OpenGL GPU renderer displays the results at a steady **60 frames per second**."

Point out:
- The **status bar** at the bottom: Scheme, Eb/N0, FPS, Msym/s, measured BER vs theoretical BER.
- The **sidebar** on the left: live controls — scheme selector, Eb/N0 slider, Run/Pause, Reset Stats.

---

### 1. Modulation & Constellation (Tab 1 — `Ctrl+1`, ~3 min)

**What to show:**
1. Select **BPSK** in the sidebar dropdown → two clusters appear on the I-Q plane.
2. Drag the **Eb/N0 slider** from 0 dB to 10 dB → watch the clusters tighten in real time.
3. Switch to **16-QAM** → 16 distinct clusters appear, demonstrating higher spectral efficiency.
4. Switch to **64-QAM** → clusters compress, illustrating the SNR requirement for high-order modulations.

**What to say:**
> "The GPU density renderer accumulates 1 million IQ symbol points per second with an exponential decay — just like a phosphor oscilloscope. The constellation shows the trade-off: BPSK is robust but carries only 1 bit per symbol, while 64-QAM carries 6 bits per symbol but needs much higher Eb/N0 to maintain a low bit-error rate."

**Theory link:** Shannon spectral efficiency $\eta = \log_2 M$ bits/s/Hz; BER = $Q(\sqrt{2 E_b/N_0})$ for BPSK.

---

### 2. Eye Diagram & Pulse Shaping (Tab 2 — `Ctrl+2`, ~3 min)

**What to show:**
1. Enable the **RRC Pulse Shaping Filter** checkbox. Watch the eye diagram open wide.
2. Adjust **Roll-off Beta** from 0.1 to 1.0 — observe how the eye becomes wider but uses more bandwidth.
3. Enable **Multipath ISI Channel [1, 0.5, 0.3]** — the eye collapses completely to 0.00 opening.
4. Disable ISI → eye reopens.

**What to say:**
> "The eye diagram is the standard tool for measuring ISI. With the Root-Raised-Cosine matched filter, the Nyquist criterion is satisfied — zero ISI at sampling instants — so the eye is wide open. When we turn on the 3-tap multipath channel (representing reflections with delays of 1 and 2 symbol periods), the worst-case vertical opening drops by 0.5 + 0.3 = 0.8, closing the eye and degrading BER."

**Theory link:** Nyquist criterion $h(kT_s) = \delta_{k0}$; multipath eye closure = $1 - \sum_{k\neq0} |a_k|$.

---

### 3. BER & Coding Gain (Tab 3 — `Ctrl+3`, ~3 min)

**What to show:**
1. Point to the **BER waterfall plot** with 4 curves.
2. Identify the **operating point** (white dot) — drag the Eb/N0 slider in the sidebar and watch it move in real time.
3. Highlight the **Soft Viterbi K=7** curve versus uncoded BPSK — a **5.43 dB** coding gain at BER = 10⁻⁵.
4. Click **Run CRC-16 Burst Verification** → shows "100% detected".
5. Click **Run Hamming(7,4) Single Error Verification** → shows "112/112 corrected".

**What to say:**
> "These are measured values, not estimates. We ran 10 million bits through the Viterbi decoder at 0.25 dB steps. The soft-decision Viterbi (using log-likelihood ratios) achieves a coding gain of 5.43 dB over uncoded BPSK — meaning we need 5.43 dB less transmit power to reach the same error rate. The CRC-16 catches 100% of all burst errors up to 16 bits."

**Theory link:** Viterbi MLSE; soft coding gain ≈ 5.5 dB, hard ≈ 3.5 dB at BER 10⁻⁵ (K=7, rate 1/2).

---

### 4. Shannon Capacity (Tab 4 — `Ctrl+4`, ~2 min)

**What to show:**
1. Point to the **Shannon bound curve**.
2. Show the constellation **operating points** (dots) sitting above the curve.
3. Read off the Shannon gap from the table: BPSK has ~8 dB gap, 64-QAM ~11.8 dB gap.

**What to say:**
> "The Shannon-Hartley theorem sets the absolute theoretical limit on spectral efficiency. Every real modulation scheme sits above this bound — the gap to capacity is the inefficiency we pay due to imperfect coding and simple constellation shaping. LDPC codes in modern Wi-Fi 6 close this gap to under 1 dB."

**Theory link:** $C/W = \log_2(1 + \eta \cdot E_b/N_0)$; Shannon limit = −1.59 dB as $\eta \to 0$.

---

### 5. Analog Modulation & Spectrum (Tab 5 — `Ctrl+5`, ~2 min)

**What to show:**
1. Adjust the **fm** and **Delta_f** sliders — watch the Carson's Rule bandwidth update instantly.
2. If the spectrum probe is active, point to the **PSD plot** with the FM sideband tones.
3. Adjust the **PCM Quantizer** bits from 2 to 12 — SQNR increases by 6 dB per bit.

**What to say:**
> "Carson's rule states that 98% of FM signal power lies within $B_T = 2(\Delta f + f_m)$. We verified this to within 1.3% in our tests. The PCM quantizer SQNR follows the classic formula 6.02N + 1.76 dB — we measured this within 0.18 dB for all N from 2 to 12 bits."

---

### 6. Multiple Access (Tab 6 — `Ctrl+6`, ~2 min)

**What to show:**
1. Set **Active Users = 12**, leave **Walsh Codes enabled** → MUI = 0.000.
2. Switch to **PN Spreading** → MUI variance = (12−1)/16 = 0.6875 appears.

**What to say:**
> "With orthogonal Walsh-Hadamard codes and a spreading factor of 16, we can support up to 16 simultaneous users with **zero** multi-user interference. We verified 0 bit errors across 1.2 million bits. With random PN sequences, the cross-correlation leaks, and BER degrades monotonically as users increase — exactly as the theory predicts."

**Theory link:** $\langle w_i, w_j \rangle = SF \cdot \delta_{ij}$; PN MUI variance = $(U-1)/SF$.

---

### 7. Protocol & ARQ (Tab 7 — `Ctrl+7`, ~2 min)

**What to show:**
1. Adjust **Frame Loss Pf = 0.15** and **Delay a = 0.5**.
2. Set **Window W = 4** — read off Stop-and-Wait vs Go-Back-N efficiency.
3. Click **Run Monte Carlo Verification** → shows simulated vs theoretical efficiency.
4. Switch to the **Packet Inspection** tab at the bottom — point out the full TCP/IPv4/Ethernet II dissection with CRC-32 verification.

**What to say:**
> "The simulation implements the full RFC-compliant encapsulation stack. You can see every byte of the Ethernet frame, the IPv4 TTL, and the TCP sequence numbers. The ARQ efficiency formulas match the simulation within 0.39% — well within the 5% tolerance we set."

---

### 8. Performance Numbers (Sidebar, ~1 min)

Point to the **status bar** and sidebar telemetry:

| Metric | Measured | Target |
|---|---|---|
| Uncoded BPSK throughput | **115.8 Mbit/s** | ≥ 20 |
| 16-QAM throughput | **170.5 Mbit/s** | ≥ 15 |
| Soft Viterbi info rate | **5.01 Mbit/s** | ≥ 1 |
| Viewer frame rate | **60 FPS** | 60 FPS |
| Parameter change latency | **60 ms** | < 100 ms |

> "Every number on screen is live-measured. The simulation runs entirely deterministically — the same seed always produces bit-identical results whether run on 1 or 8 threads."

---

## Expected Professor Questions & Answers

| Question | Answer |
|---|---|
| *How is the BER computed?* | Bit counter in the sim thread: errors/total. Resets with the R key or Reset Stats button. |
| *Why use counter-based RNG?* | PCG keyed by (seed, block_id, sample_index) guarantees determinism regardless of thread scheduling order. |
| *What is the coding gain?* | Soft Viterbi K=7 rate-1/2: **5.43 dB** at BER 10⁻⁵, measured at 0.25 dB steps with ≥100 bit errors per point. |
| *Why does the eye close with ISI?* | Multipath reflections with delays of 1 and 2 symbol periods add constructively at off-peak times, reducing the vertical opening from 1.0 to 0.0 for taps [1, 0.5, 0.3]. |
| *Is this GPU accelerated?* | The density renderer uses an OpenGL 3.3 framebuffer (GL_R32F), accumulating symbol hits and applying per-frame exponential decay in a GLSL fragment shader. |
| *How many tests?* | **62 Catch2 unit tests** covering all milestones M0–M8. Run `ctest --test-dir build --output-on-failure` to see all 62 pass. |
| *Why C++17?* | For performance: smart pointers (no raw owning pointers), `std::atomic` for the wait-free triple buffer, structured bindings, and `if constexpr` for template blocks. |

---

## Keyboard Shortcuts Cheat Sheet

| Key | Action |
|---|---|
| `Ctrl+1` … `Ctrl+7` | Switch to tab 1–7 |
| `Space` | Pause / Resume simulation |
| `R` | Reset statistics and density buffers |
| `F11` | Toggle fullscreen |
| `Ctrl+=` / `Ctrl+-` | Increase / Decrease UI scale |

---

## Pre-Demo Checklist

- [ ] Run `ctest --test-dir build --output-on-failure` — confirm **62/62 pass**
- [ ] Application launches, status bar shows 60 FPS and ~1 Msym/s
- [ ] Eb/N0 slider works — BER updates live in status bar
- [ ] RRC toggle opens the eye diagram, ISI toggle closes it
- [ ] BER waterfall plot visible in Tab 3 with 4 curves + live operating point
- [ ] Packet dissection table in bottom dock shows valid CRC-32 hex
- [ ] `docs/report.pdf` compiled and ready to open alongside the demo
