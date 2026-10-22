import os, numpy as np, pandas as pd, matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from math import erfc, sqrt
os.makedirs("results/figs", exist_ok=True)
plt.rcParams.update({"font.size": 10, "axes.grid": True, "grid.alpha": .3})
Q = lambda x: 0.5 * erfc(x / sqrt(2))

# 1. BER curves
d = pd.read_csv("results/ber_sweep.csv"); fig, ax = plt.subplots(figsize=(7, 5))
for i, (m, g) in enumerate(d.groupby("modem", sort=False)):
    c = f"C{i}"; ax.semilogy(g.ebn0_db, g.theory, "--", color=c, lw=1)
    g = g[g.errors >= 10]; ax.semilogy(g.ebn0_db, g.ber, "o", color=c, ms=5, label=m)
ax.set_ylim(1e-6, 1); ax.set_xlabel("Eb/N0 (dB)"); ax.set_ylabel("BER")
ax.set_title("BER over AWGN: simulation (markers) vs theory (dashed)"); ax.legend()
fig.tight_layout(); fig.savefig("results/figs/ber_curves.png", dpi=150)

# 2. coding gain
d = pd.read_csv("results/coding_gain.csv"); fig, ax = plt.subplots(figsize=(7, 5))
for m, g in d.groupby("scheme", sort=False):
    g = g[g.errors >= 8]; ax.semilogy(g.ebn0_db, g.ber, "o-", ms=4, label=m)
x = np.linspace(0, 7, 100); ax.semilogy(x, [Q(sqrt(2 * 10 ** (v / 10))) for v in x], "k--", lw=.8, label="BPSK theory")
ax.set_ylim(1e-6, 1); ax.set_xlabel("Eb/N0 (dB)"); ax.set_ylabel("Information BER")
ax.set_title("Channel coding gain"); ax.legend(); fig.tight_layout(); fig.savefig("results/figs/coding_gain.png", dpi=150)

# 3. CDMA
d = pd.read_csv("results/cdma.csv"); fig, axs = plt.subplots(1, 2, figsize=(11, 4.5), sharey=True)
for ax, (cn, gg) in zip(axs, d.groupby("codes", sort=False)):
    for u, g in gg.groupby("users"):
        g = g[g.ber > 0]; ax.semilogy(g.ebn0_db, g.ber, "o-", ms=3, label=f"{u} user(s)")
    ax.set_title(f"CDMA, SF=16, {cn} codes"); ax.set_xlabel("Eb/N0 (dB)"); ax.set_ylim(1e-5, 1); ax.legend()
axs[0].set_ylabel("BER (user 1)"); fig.tight_layout(); fig.savefig("results/figs/cdma.png", dpi=150)

# 4. PCM + DM
d = pd.read_csv("results/pcm_snr.csv"); fig, axs = plt.subplots(1, 2, figsize=(11, 4.2))
axs[0].plot(d.bits, d.snr_db, "o-", label="simulated"); axs[0].plot(d.bits, d.theory_db, "k--", label="6.02N + 1.76 dB")
axs[0].set_xlabel("bits per sample N"); axs[0].set_ylabel("SNR (dB)"); axs[0].set_title("PCM quantisation SNR"); axs[0].legend()
m = pd.read_csv("results/dm.csv"); axs[1].plot(m.n, m.input, "k", lw=2, label="input")
for c in ["step_0.02", "step_0.1", "step_0.4"]: axs[1].step(m.n, m[c], where="post", lw=1, label=c.replace("_", " = "))
axs[1].set_title("Delta modulation: slope overload (small step) vs granular noise (large step)"); axs[1].title.set_fontsize(9)
axs[1].set_xlabel("sample"); axs[1].legend(fontsize=8); fig.tight_layout(); fig.savefig("results/figs/pcm_dm.png", dpi=150)

# 5. eye diagrams
d = pd.read_csv("results/eye.csv"); cs = list(dict.fromkeys(d.case)); fig, axs = plt.subplots(1, 3, figsize=(13, 4), sharey=True)
for ax, c in zip(axs, cs):
    for _, g in d[d.case == c].groupby("trace"): ax.plot(g["sample"], g["value"], color="C0", alpha=.15, lw=.7)
    ax.axvline(0, color="r", lw=.8, ls=":"); ax.set_title(c); ax.set_xlabel("time (symbol periods)")
axs[0].set_ylabel("amplitude"); fig.tight_layout(); fig.savefig("results/figs/eye.png", dpi=150)
print("figures written to results/figs/")
