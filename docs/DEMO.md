# commsim-studio: 3-Minute Live Interactive Demo Script

This script guides an evaluator or instructor through a live demonstration of `commsim-studio` covering the primary physical layer and protocol principles within 3 minutes.

---

## Prerequisites

Build the Release binary and launch the viewer:
```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/app/commsim_studio
```

---

## Timeline & Step-by-Step Flow

### [00:00 - 00:45] Modulation Constellations & Phosphor Persistence (M1, M3, M6)
1. **Launch Viewer**: The application window opens showing real-time multi-panel telemetry at 60 FPS.
2. **Start with BPSK**:
   - In the **Controls & Configuration** panel, select `BPSK`.
   - Set $E_b/N_0 = 6.0\text{ dB}$.
   - Observe two sharp phosphor clusters in the **Constellation Diagram** (GPU density renderer).
   - Check the **BER Curve** readout: live measured BER aligns with the theoretical curve $Q(\sqrt{2 E_b/N_0}) \approx 2.39 \times 10^{-3}$.
3. **Switch to QPSK and 16-QAM**:
   - Change Scheme to `QPSK`: 4 symmetric quadrants appear instantaneously (< 100 ms parameter latency).
   - Change Scheme to `16-QAM` and set $E_b/N_0 = 10.0\text{ dB}$:
   - Observe the 16 distinct square constellation clusters rendered via GPU density accumulation with decay.

---

### [00:45 - 01:30] Nyquist Pulse Shaping, Inter-Symbol Interference (ISI), & Eye Closure (M4)
1. **Examine the Eye Diagram**:
   - Look at the **Eye Diagram Panel**.
   - With Root-Raised-Cosine (RRC) matched filtering ($\beta = 0.35$), observe the wide, clean central eye opening (100% open, zero ISI at symbol sampling instants).
2. **Inject Multi-path ISI Channel Distortion**:
   - In the **Pulse Shaping & ISI Panel**, check `Enable ISI Channel` with discrete multipath taps $[1.0, 0.5, 0.3]$.
   - Watch the eye diagram immediately close as inter-symbol interference smears the trajectories.
   - Adjust the **Timing Offset Slider**: notice the dramatic degradation in eye height and vertical margin.

---

### [01:30 - 02:15] Convolutional Coding & The Viterbi BER Cliff (M5)
1. **Headless & Live Coding Comparison**:
   - In the **Coding & FEC Panel**, toggle between `Uncoded BPSK` and `Rate-1/2 K=7 Convolutional Code` (octal generators $171_8, 133_8$).
   - At $E_b/N_0 = 4.0\text{ dB}$, uncoded BPSK exhibits a BER of $\sim 1.25 \times 10^{-2}$.
   - Switch to **Soft Viterbi Decoding**: watch the bit errors drop to zero (BER $< 10^{-5}$), showcasing the steep **coding gain waterfall cliff** of over $5.0\text{ dB}$.
2. **Burst Error Protection**:
   - Demonstrate the **Block Interleaver**: show how interleaved data disperses concentrated noise bursts so the Viterbi decoder corrects them seamlessly.

---

### [02:15 - 03:00] Multiple Access (CDMA 8-12 Users) & Protocol Encapsulation (M7)
1. **CDMA Multi-User Interference**:
   - Navigate to the **Protocol & Multiple Access (M7)** panel.
   - Set active users $U = 8$ (then $U = 12$) under **Orthogonal Walsh Spreading** ($SF = 16$).
   - Point out that measured Multi-User Interference (MUI) is strictly **zero** ($\langle w_i, w_j \rangle = 0$): each user achieves 100% error-free reception in a noiseless channel and identical single-user theoretical BER under AWGN.
   - Switch from Walsh to **Pseudo-Noise (PN) Spreading**:
   - Observe how cross-correlation variance $\sigma_{\text{MUI}}^2 = (U - 1)/SF$ causes the effective SINR to degrade and the BER to rise strictly monotonically with user count.
2. **Live OSI / TCP-IP Encapsulation View**:
   - In the packet inspector, enter custom application payload text (e.g. `GET /telemetry HTTP/1.1`).
   - Inspect the real-time serialized byte offsets:
     - Ethernet II (14 bytes MAC + EtherType `0x0800`)
     - IPv4 (20 bytes with valid RFC 791 header checksum)
     - TCP (20 bytes with valid RFC 793 pseudo-header checksum)
     - IEEE 802.3 32-bit CRC Frame Check Sequence (FCS)
3. **ARQ Protocol Efficiency**:
   - Slide frame loss probability $P_f$ and propagation delay $a$.
   - Confirm live simulated throughput efficiency matches Stallings theoretical formulas $\eta_{\text{SW}} = \frac{1 - P_f}{1 + 2a}$ and $\eta_{\text{GBN}} = \frac{1 - P_f}{1 + 2a P_f}$ within 1%.

---

## Summary Checklist
- [x] 60 FPS viewer rendering with GPU density persistence
- [x] Clean modulation constellation progression (BPSK $\to$ QPSK $\to$ 16-QAM)
- [x] Eye closure demonstration with multipath taps $[1, 0.5, 0.3]$
- [x] Over $5\text{ dB}$ coding gain cliff with Rate-1/2 $K=7$ Soft Viterbi
- [x] CDMA 8 to 12 user orthogonality proof (Walsh zero MUI vs. PN degradation)
- [x] Real-time RFC-compliant packet encapsulation and ARQ analysis
