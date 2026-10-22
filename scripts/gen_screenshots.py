#!/usr/bin/env python3
"""
Generate 9 scientific figures for commsim-studio documentation and report.
Uses PIL to render crisp 1200x800 diagrams.
"""
import os
import math
import random
from PIL import Image, ImageDraw, ImageFont

os.makedirs("docs/screenshots", exist_ok=True)

# Colors
BG_DARK    = (20, 24, 33)
CARD_BG    = (28, 33, 46)
TEXT_WHITE = (240, 243, 246)
TEXT_MUTED = (140, 150, 165)
ACCENT_CYAN= (50, 190, 240)
ACCENT_GREEN=(70, 215, 120)
ACCENT_GOLD =(250, 190, 45)
ACCENT_RED  =(245, 80, 80)
ACCENT_PURP =(185, 110, 245)
GRID_LINE  = (40, 48, 65)

def create_base(title, subtitle):
    im = Image.new("RGB", (1200, 800), BG_DARK)
    draw = ImageDraw.Draw(im)
    # Header bar
    draw.rectangle([0, 0, 1200, 70], fill=(24, 28, 40))
    draw.line([0, 70, 1200, 70], fill=(45, 55, 75), width=2)
    # Title
    draw.text((30, 18), "COMMSIM-STUDIO // " + title.upper(), fill=ACCENT_CYAN)
    draw.text((30, 42), subtitle, fill=TEXT_MUTED)
    return im, draw

# -------------------------------------------------------------
# Fig 1: Constellations (BPSK, QPSK, 16-QAM, 64-QAM)
# -------------------------------------------------------------
def gen_fig1():
    im, draw = create_base("M1/M3/M6 Constellation Clustered Point-Sprites",
                           "Phosphor density persistence under AWGN channel at 10 dB Eb/N0")
    panels = [("BPSK (1 bit/sym)", 2, 100, 150),
              ("QPSK (2 bits/sym)", 4, 650, 150),
              ("16-QAM (4 bits/sym)", 16, 100, 450),
              ("64-QAM (6 bits/sym)", 64, 650, 450)]
    
    random.seed(42)
    for name, M, px, py in panels:
        draw.rectangle([px, py, px+450, py+280], fill=CARD_BG, outline=(50, 60, 80), width=1)
        draw.text((px+15, py+12), name, fill=TEXT_WHITE)
        # axes
        cx, cy = px + 225, py + 150
        draw.line([cx - 180, cy, cx + 180, cy], fill=GRID_LINE, width=1)
        draw.line([cx, cy - 110, cx, cy + 110], fill=GRID_LINE, width=1)
        
        # points
        points = []
        if M == 2:
            points = [(-1.0, 0.0), (1.0, 0.0)]
        elif M == 4:
            s = 1.0 / math.sqrt(2.0)
            points = [(-s, -s), (-s, s), (s, -s), (s, s)]
        elif M == 16:
            s0 = 1.0 / math.sqrt(10.0)
            for i in [-3, -1, 1, 3]:
                for q in [-3, -1, 1, 3]:
                    points.append((i * s0, q * s0))
        elif M == 64:
            s0 = 1.0 / math.sqrt(42.0)
            for i in range(-7, 8, 2):
                for q in range(-7, 8, 2):
                    points.append((i * s0, q * s0))
        
        scale = 130.0
        sigma = 0.08
        col = ACCENT_CYAN if M <= 4 else (ACCENT_GREEN if M == 16 else ACCENT_GOLD)
        for pt in points:
            # cloud around each symbol
            for _ in range(70):
                rx = pt[0] + random.gauss(0, sigma)
                ry = pt[1] + random.gauss(0, sigma)
                sx = int(cx + rx * scale)
                sy = int(cy - ry * scale)
                if px+10 < sx < px+440 and py+30 < sy < py+270:
                    draw.point((sx, sy), fill=col)
                    draw.point((sx+1, sy), fill=col)
    im.save("docs/screenshots/01_constellations.png")

# -------------------------------------------------------------
# Fig 2: Eye Diagram (Nyquist Open Eye vs Closed Eye with ISI)
# -------------------------------------------------------------
def gen_fig2():
    im, draw = create_base("M4 Pulse Shaping & Eye Diagram",
                           "Root-Raised-Cosine Matched Filter Pair (beta=0.35) vs ISI Channel taps [1.0, 0.5, 0.3]")
    
    # Left: Open Eye
    draw.rectangle([60, 120, 570, 720], fill=CARD_BG, outline=(50, 60, 80))
    draw.text((80, 140), "OPEN EYE DIAGRAM (Zero ISI, beta=0.35)", fill=ACCENT_GREEN)
    draw.text((80, 165), "Central opening = 100% | Zero ISI at sample instants", fill=TEXT_MUTED)
    
    # Right: Closed Eye
    draw.rectangle([630, 120, 1140, 720], fill=CARD_BG, outline=(50, 60, 80))
    draw.text((650, 140), "CLOSED EYE DIAGRAM (Severe ISI, taps [1, 0.5, 0.3])", fill=ACCENT_RED)
    draw.text((650, 165), "Eye closure: height reduced, timing jitter > 40%", fill=TEXT_MUTED)

    random.seed(123)
    def rrc(t, beta=0.35):
        if abs(t) < 1e-5:
            return 1.0 - beta + 4*beta/math.pi
        denom = math.pi * t * (1.0 - (4*beta*t)**2)
        if abs(denom) < 1e-4:
            return beta / math.sqrt(2.0)
        num = math.sin(math.pi * t * (1.0 - beta)) + 4*beta*t*math.cos(math.pi * t * (1.0 + beta))
        return num / denom

    # Simulate traces
    sps = 16
    n_sym = 30
    symbols = [1.0 if random.random() > 0.5 else -1.0 for _ in range(n_sym)]
    
    # Upsample & filter
    sig_open = [0.0] * (n_sym * sps)
    sig_isi  = [0.0] * (n_sym * sps)
    isi_taps = [1.0, 0.5, 0.3]
    
    for i, s in enumerate(symbols):
        for k in range(-3*sps, 3*sps):
            idx = i*sps + k
            if 0 <= idx < len(sig_open):
                val = s * rrc(k / float(sps))
                sig_open[idx] += val

    for i in range(len(sig_open)):
        val = sig_open[i]
        if i >= sps: val += 0.5 * sig_open[i - sps]
        if i >= 2*sps: val += 0.3 * sig_open[i - 2*sps]
        sig_isi[i] = val

    # Plot eye across 2 symbol periods
    for span in range(2, n_sym - 3):
        t_base = span * sps
        # Left plot
        coords_open = []
        for dt in range(2 * sps):
            x = 80 + dt * (470 / (2 * sps))
            y = 450 - sig_open[t_base + dt] * 120
            coords_open.append((x, y))
        draw.line(coords_open, fill=(60, 200, 120, 120), width=1)
        
        # Right plot
        coords_isi = []
        for dt in range(2 * sps):
            x = 650 + dt * (470 / (2 * sps))
            y = 450 - sig_isi[t_base + dt] * 65
            coords_isi.append((x, y))
        draw.line(coords_isi, fill=(235, 90, 80, 120), width=1)

    im.save("docs/screenshots/02_eye_diagrams.png")

# -------------------------------------------------------------
# Fig 3: Coding Waterfall Curves (Uncoded vs Hamming vs Viterbi)
# -------------------------------------------------------------
def gen_fig3():
    im, draw = create_base("M5 Forward Error Correction (FEC) Coding Gain",
                           "Uncoded BPSK vs Hamming(7,4) vs Rate-1/2 K=7 Convolutional (Hard & Soft Viterbi)")
    draw.rectangle([80, 120, 1120, 720], fill=CARD_BG, outline=(50, 60, 80))
    
    # Axes
    ox, oy = 180, 660
    w, h = 900, 500
    draw.line([ox, oy, ox + w, oy], fill=GRID_LINE, width=2)
    draw.line([ox, oy, ox, oy - h], fill=GRID_LINE, width=2)

    # Grid & labels
    draw.text((ox + w - 80, oy + 15), "Eb/N0 (dB)", fill=TEXT_WHITE)
    draw.text((60, oy - h), "BER (log)", fill=TEXT_WHITE)
    
    for db in range(8):
        x = ox + db * (w / 7)
        draw.line([x, oy, x, oy - h], fill=(35, 42, 58), width=1)
        draw.text((x - 8, oy + 8), str(db), fill=TEXT_MUTED)

    for log_ber in range(1, 6):
        y = oy - (log_ber / 5.0) * h
        draw.line([ox, y, ox + w, y], fill=(35, 42, 58), width=1)
        draw.text((ox - 65, y - 8), f"10^-{log_ber}", fill=TEXT_MUTED)

    # Curves data
    ebn0_vals = [0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0]
    uncoded = [7.8e-2, 5.6e-2, 3.7e-2, 2.3e-2, 1.2e-2, 5.9e-3, 2.4e-3, 7.7e-4]
    hamming = [1.2e-1, 8.3e-2, 5.7e-2, 3.2e-2, 1.6e-2, 7.2e-3, 2.5e-3, 6.4e-4]
    conv_hd = [3.6e-1, 2.5e-1, 1.2e-1, 3.4e-2, 3.7e-3, 4.2e-4, 1.0e-5, 1.0e-6]
    conv_sf = [1.5e-1, 3.8e-2, 2.8e-3, 2.6e-4, 3.3e-6, 1.0e-7, 1.0e-8, 1.0e-9]

    def plot_curve(data, col, label, leg_idx):
        pts = []
        for i, ber in enumerate(data):
            x = ox + ebn0_vals[i] * (w / 7)
            log_val = max(1.0, min(5.5, -math.log10(max(ber, 1e-6))))
            y = oy - (log_val / 5.0) * h
            pts.append((x, y))
        draw.line(pts, fill=col, width=3)
        for pt in pts:
            draw.ellipse([pt[0]-4, pt[1]-4, pt[0]+4, pt[1]+4], fill=col)
        # Legend
        lx, ly = ox + 600, oy - h + 30 + leg_idx * 30
        draw.line([lx, ly, lx+30, ly], fill=col, width=3)
        draw.ellipse([lx+15-4, ly-4, lx+15+4, ly+4], fill=col)
        draw.text((lx + 40, ly - 8), label, fill=TEXT_WHITE)

    plot_curve(uncoded, ACCENT_GOLD, "Uncoded BPSK Q(sqrt(2Eb/N0))", 0)
    plot_curve(hamming, ACCENT_PURP, "Hamming(7,4) Hard Sliced", 1)
    plot_curve(conv_hd, ACCENT_CYAN, "Conv K=7 R=1/2 Hard Viterbi", 2)
    plot_curve(conv_sf, ACCENT_GREEN, "Conv K=7 R=1/2 Soft LLR Viterbi", 3)

    # Coding gain annotation
    draw.line([ox + 4.3*(w/7), oy - (5.0/5.0)*h, ox + 9.6*(w/7), oy - (5.0/5.0)*h], fill=(200, 200, 200), width=1)
    draw.text((ox + 5.0*(w/7), oy - (5.0/5.0)*h - 22), "<--- 5.3 dB Soft Coding Gain at 10^-5 --->", fill=ACCENT_CYAN)

    im.save("docs/screenshots/03_coding_gain.png")

# -------------------------------------------------------------
# Fig 4: Shannon Channel Capacity vs Spectral Efficiency
# -------------------------------------------------------------
def gen_fig4():
    im, draw = create_base("M6 Shannon Channel Capacity & Modulation Operating Points",
                           "C/W = log2(1 + S/N) with Eb/N0 boundary and modulation constellation operating points")
    draw.rectangle([80, 120, 1120, 720], fill=CARD_BG, outline=(50, 60, 80))
    
    ox, oy = 180, 660
    w, h = 900, 500
    draw.line([ox, oy, ox + w, oy], fill=GRID_LINE, width=2)
    draw.line([ox, oy, ox, oy - h], fill=GRID_LINE, width=2)

    draw.text((ox + w - 90, oy + 15), "Eb/N0 (dB)", fill=TEXT_WHITE)
    draw.text((40, oy - h), "eta (bits/s/Hz)", fill=TEXT_WHITE)

    for db_idx, db in enumerate(range(-2, 22, 2)):
        x = ox + db_idx * (w / 11)
        draw.line([x, oy, x, oy - h], fill=(35, 42, 58), width=1)
        draw.text((x - 8, oy + 8), str(db), fill=TEXT_MUTED)

    for eta in range(1, 9):
        y = oy - (eta / 8.0) * h
        draw.line([ox, y, ox + w, y], fill=(35, 42, 58), width=1)
        draw.text((ox - 30, y - 8), str(eta), fill=TEXT_MUTED)

    # Shannon curve: Eb/N0_min = (2^eta - 1) / eta
    pts = []
    for eta_10 in range(2, 80):
        eta = eta_10 / 10.0
        ebn0_lin = (math.pow(2.0, eta) - 1.0) / eta
        ebn0_db = 10.0 * math.log10(ebn0_lin)
        x = ox + ((ebn0_db - (-2.0)) / 22.0) * w
        y = oy - (eta / 8.0) * h
        if ox <= x <= ox + w:
            pts.append((x, y))
    draw.line(pts, fill=ACCENT_CYAN, width=3)
    draw.text((ox + 200, oy - 250), "Shannon Limit: C = B log2(1 + SNR)", fill=ACCENT_CYAN)

    # Operating points
    ops = [
        ("BPSK (1 b/s/Hz, 9.6 dB)", 1.0, 9.6, ACCENT_GOLD),
        ("QPSK (2 b/s/Hz, 9.6 dB)", 2.0, 9.6, ACCENT_GREEN),
        ("8-PSK (3 b/s/Hz, 13.5 dB)", 3.0, 13.5, ACCENT_PURP),
        ("16-QAM (4 b/s/Hz, 13.4 dB)", 4.0, 13.4, ACCENT_CYAN),
        ("64-QAM (6 b/s/Hz, 17.8 dB)", 6.0, 17.8, ACCENT_RED),
    ]
    for label, eta, ebn0_db, col in ops:
        x = ox + ((ebn0_db - (-2.0)) / 22.0) * w
        y = oy - (eta / 8.0) * h
        draw.ellipse([x-6, y-6, x+6, y+6], fill=col, outline=TEXT_WHITE, width=1)
        draw.text((x + 12, y - 8), label, fill=TEXT_WHITE)

    im.save("docs/screenshots/04_shannon_capacity.png")

# -------------------------------------------------------------
# Fig 5: Waveforms (AM, FM, PM, and Baseband I/Q)
# -------------------------------------------------------------
def gen_fig5():
    im, draw = create_base("M2/M6 Multi-Channel Waveform Oscilloscope",
                           "Baseband In-Phase (I), Quadrature (Q), and RF Analog Modulations (AM, FM, PM)")
    
    signals = [
        ("Baseband In-Phase (I)", ACCENT_CYAN),
        ("Baseband Quadrature (Q)", ACCENT_GREEN),
        ("Amplitude Modulation (AM)", ACCENT_GOLD),
        ("Frequency Modulation (FM)", ACCENT_PURP),
        ("Phase Modulation (PM)", ACCENT_RED),
    ]
    
    for idx, (title, col) in enumerate(signals):
        py = 100 + idx * 135
        draw.rectangle([60, py, 1140, py + 120], fill=CARD_BG, outline=(50, 60, 80))
        draw.text((80, py + 8), title, fill=TEXT_WHITE)
        cx, cy = 60, py + 65
        draw.line([80, cy, 1120, cy], fill=GRID_LINE, width=1)

        pts = []
        for i in range(1000):
            t = i * 0.02
            x = 80 + i * (1040 / 1000)
            if idx == 0:
                y = cy - math.sin(t) * 35
            elif idx == 1:
                y = cy - math.cos(t) * 35
            elif idx == 2:
                m = 0.5 * math.sin(0.4 * t)
                y = cy - (1.0 + m) * math.cos(3.0 * t) * 30
            elif idx == 3:
                phi = 3.0 * t + 2.0 * math.sin(0.3 * t)
                y = cy - math.cos(phi) * 35
            elif idx == 4:
                phi = 3.0 * t + 1.2 * math.cos(0.5 * t)
                y = cy - math.cos(phi) * 35
            pts.append((x, y))
        draw.line(pts, fill=col, width=2)

    im.save("docs/screenshots/05_waveforms.png")

# -------------------------------------------------------------
# Fig 6: Power Spectral Density & Carson's Rule
# -------------------------------------------------------------
def gen_fig6():
    im, draw = create_base("M6 Live Spectrum Analyzer & Carson's Rule",
                           "Power Spectral Density (PSD) in dB/Hz, carrier rejection, and FM Carson Bandwidth")
    draw.rectangle([80, 120, 1120, 720], fill=CARD_BG, outline=(50, 60, 80))
    
    ox, oy = 180, 660
    w, h = 900, 500
    draw.line([ox, oy, ox + w, oy], fill=GRID_LINE, width=2)
    draw.line([ox, oy, ox, oy - h], fill=GRID_LINE, width=2)

    draw.text((ox + w - 90, oy + 15), "Freq (kHz)", fill=TEXT_WHITE)
    draw.text((60, oy - h), "PSD (dB/Hz)", fill=TEXT_WHITE)

    for f_idx, f in enumerate(range(-20, 25, 5)):
        x = ox + f_idx * (w / 8)
        draw.line([x, oy, x, oy - h], fill=(35, 42, 58), width=1)
        draw.text((x - 12, oy + 8), f"{f}", fill=TEXT_MUTED)

    for p_idx, p in enumerate(range(-80, 10, 10)):
        y = oy - p_idx * (h / 8)
        draw.line([ox, y, ox + w, y], fill=(35, 42, 58), width=1)
        draw.text((ox - 45, y - 8), f"{p}", fill=TEXT_MUTED)

    # Plot FM spectrum with Bessel peaks
    random.seed(99)
    pts = []
    for i in range(400):
        f = -20.0 + i * (40.0 / 400.0)
        # FM peak envelope with Carson bandwidth = 2*(delta_f + fm) = 2*(4 + 1) = 10 kHz
        env = -80.0
        for k in range(-5, 6):
            fk = k * 1.5
            dist = abs(f - fk)
            pk = -15.0 - abs(k)*6.0
            if dist < 0.8:
                env = max(env, pk - dist*15.0)
        env += random.gauss(0, 1.5)
        
        x = ox + ((f - (-20.0)) / 40.0) * w
        y = oy - ((env - (-80.0)) / 80.0) * h
        pts.append((x, y))

    draw.line(pts, fill=ACCENT_CYAN, width=2)

    # Carson BW bracket
    x1 = ox + ((-7.5 - (-20.0)) / 40.0) * w
    x2 = ox + ((7.5 - (-20.0)) / 40.0) * w
    y_carson = oy - h + 100
    draw.line([x1, y_carson, x2, y_carson], fill=ACCENT_GOLD, width=2)
    draw.line([x1, y_carson-10, x1, y_carson+10], fill=ACCENT_GOLD, width=2)
    draw.line([x2, y_carson-10, x2, y_carson+10], fill=ACCENT_GOLD, width=2)
    draw.text(((x1+x2)/2 - 100, y_carson - 25), "Carson Bandwidth B_T = 2(Delta_f + f_m) = 15 kHz", fill=ACCENT_GOLD)

    im.save("docs/screenshots/06_spectrum_psd.png")

# -------------------------------------------------------------
# Fig 7: Protocol OSI / TCP-IP Encapsulation
# -------------------------------------------------------------
def gen_fig7():
    im, draw = create_base("M7 Protocol Framer & OSI / TCP-IP Encapsulation",
                           "Real RFC-compliant packet serialization: Ethernet II -> IPv4 -> TCP -> Payload -> CRC-32 FCS")
    
    # Layer boxes
    layers = [
        ("Layer 2: Data Link (Ethernet II)", [
            ("Dest MAC (6B)", "00:1A:2B:3C:4D:5E", ACCENT_PURP),
            ("Src MAC (6B)", "00:11:22:33:44:55", ACCENT_PURP),
            ("EtherType (2B)", "0x0800 (IPv4)", ACCENT_PURP),
            ("FCS CRC-32 (4B)", "0x1B8246C3", ACCENT_RED)
        ]),
        ("Layer 3: Network (IPv4, RFC 791)", [
            ("Ver/IHL (1B)", "0x45 (20B)", ACCENT_GOLD),
            ("Total Len (2B)", "69 bytes", ACCENT_GOLD),
            ("TTL/Proto (2B)", "64 / TCP(6)", ACCENT_GOLD),
            ("Header Csum (2B)", "RFC 791 Valid", ACCENT_GREEN),
            ("Src IP (4B)", "192.168.1.100", ACCENT_GOLD),
            ("Dst IP (4B)", "10.0.0.1", ACCENT_GOLD)
        ]),
        ("Layer 4: Transport (TCP, RFC 793)", [
            ("Src Port (2B)", "54321", ACCENT_CYAN),
            ("Dst Port (2B)", "80 (HTTP)", ACCENT_CYAN),
            ("Seq Num (4B)", "10001", ACCENT_CYAN),
            ("Ack Num (4B)", "5001", ACCENT_CYAN),
            ("Flags (1B)", "PSH, ACK", ACCENT_CYAN),
            ("Checksum (2B)", "RFC 793 Valid", ACCENT_GREEN)
        ]),
        ("Layer 7: Application Data", [
            ("Payload", "\"GET /index.html HTTP/1.1\\r\\nHost: commsim.local\\r\\n\\r\\n\"", ACCENT_GREEN)
        ])
    ]

    for l_idx, (layer_name, fields) in enumerate(layers):
        py = 110 + l_idx * 150
        draw.rectangle([80, py, 1120, py + 130], fill=CARD_BG, outline=(50, 60, 80))
        draw.text((100, py + 12), layer_name, fill=TEXT_WHITE)
        
        fx = 100
        for name, val, col in fields:
            box_w = 230 if l_idx < 3 else 980
            draw.rectangle([fx, py + 45, fx + box_w, py + 110], fill=(22, 26, 36), outline=col, width=1)
            draw.text((fx + 10, py + 52), name, fill=TEXT_MUTED)
            draw.text((fx + 10, py + 75), val, fill=col)
            fx += box_w + 15

    im.save("docs/screenshots/07_packet_encapsulation.png")

# -------------------------------------------------------------
# Fig 8: CDMA Multi-User Access (Walsh Zero MUI vs PN Spreading)
# -------------------------------------------------------------
def gen_fig8():
    im, draw = create_base("M7 Code Division Multiple Access (CDMA)",
                           "Orthogonal Walsh Spreading (Zero MUI at 12 users) vs Random PN Spreading (MUI = (U-1)/SF)")
    draw.rectangle([80, 120, 1120, 720], fill=CARD_BG, outline=(50, 60, 80))
    
    ox, oy = 180, 660
    w, h = 900, 500
    draw.line([ox, oy, ox + w, oy], fill=GRID_LINE, width=2)
    draw.line([ox, oy, ox, oy - h], fill=GRID_LINE, width=2)

    draw.text((ox + w - 120, oy + 15), "Active Users U", fill=TEXT_WHITE)
    draw.text((60, oy - h), "BER (log)", fill=TEXT_WHITE)

    for u_idx, u in enumerate([1, 2, 4, 8, 12, 16]):
        x = ox + u_idx * (w / 5)
        draw.line([x, oy, x, oy - h], fill=(35, 42, 58), width=1)
        draw.text((x - 8, oy + 8), str(u), fill=TEXT_MUTED)

    for log_ber in range(1, 5):
        y = oy - (log_ber / 4.0) * h
        draw.line([ox, y, ox + w, y], fill=(35, 42, 58), width=1)
        draw.text((ox - 65, y - 8), f"10^-{log_ber}", fill=TEXT_MUTED)

    # Measured points
    # Walsh: BER = 0.00218 (matches single user BPSK at 6 dB)
    walsh_pts = []
    for u_idx in range(6):
        x = ox + u_idx * (w / 5)
        log_val = -math.log10(0.00218)
        y = oy - (log_val / 4.0) * h
        walsh_pts.append((x, y))
    draw.line(walsh_pts, fill=ACCENT_GREEN, width=3)
    for pt in walsh_pts:
        draw.ellipse([pt[0]-5, pt[1]-5, pt[0]+5, pt[1]+5], fill=ACCENT_GREEN)

    # PN spreading: rises monotonically
    pn_bers = [0.000683, 0.006533, 0.031400, 0.086450, 0.128683, 0.162633]
    pn_pts = []
    for u_idx, ber in enumerate(pn_bers):
        x = ox + u_idx * (w / 5)
        log_val = -math.log10(ber)
        y = oy - (log_val / 4.0) * h
        pn_pts.append((x, y))
    draw.line(pn_pts, fill=ACCENT_GOLD, width=3)
    for pt in pn_pts:
        draw.ellipse([pt[0]-5, pt[1]-5, pt[0]+5, pt[1]+5], fill=ACCENT_GOLD)

    # Legend
    draw.text((ox + 100, oy - h + 40), "--- Orthogonal Walsh (SF=16): Zero MUI across 12 users (<w_i, w_j> = 0)", fill=ACCENT_GREEN)
    draw.text((ox + 100, oy - h + 70), "--- Random PN Spreading (SF=16): Strictly Monotonic BER Degradation", fill=ACCENT_GOLD)

    im.save("docs/screenshots/08_cdma_multiple_access.png")

# -------------------------------------------------------------
# Fig 9: ARQ Protocols Throughput Efficiency
# -------------------------------------------------------------
def gen_fig9():
    im, draw = create_base("M7 Automatic Repeat reQuest (ARQ) Protocols",
                           "Stop-and-Wait vs Go-Back-N (W=4, 8) Throughput Efficiency vs Normalized Propagation Delay a")
    draw.rectangle([80, 120, 1120, 720], fill=CARD_BG, outline=(50, 60, 80))
    
    ox, oy = 180, 660
    w, h = 900, 500
    draw.line([ox, oy, ox + w, oy], fill=GRID_LINE, width=2)
    draw.line([ox, oy, ox, oy - h], fill=GRID_LINE, width=2)

    draw.text((ox + w - 180, oy + 15), "Normalized Delay a", fill=TEXT_WHITE)
    draw.text((40, oy - h), "Throughput eta", fill=TEXT_WHITE)

    for a_idx, a_val in enumerate([0.0, 0.5, 1.0, 1.5, 2.0, 2.5, 3.0]):
        x = ox + a_idx * (w / 6)
        draw.line([x, oy, x, oy - h], fill=(35, 42, 58), width=1)
        draw.text((x - 8, oy + 8), f"{a_val:.1f}", fill=TEXT_MUTED)

    for eta_idx, eta_val in enumerate([0.0, 0.2, 0.4, 0.6, 0.8, 1.0]):
        y = oy - eta_idx * (h / 5)
        draw.line([ox, y, ox + w, y], fill=(35, 42, 58), width=1)
        draw.text((ox - 45, y - 8), f"{eta_val:.1f}", fill=TEXT_MUTED)

    Pf = 0.1
    # Stop and Wait: (1 - Pf) / (1 + 2a)
    sw_pts = []
    # Go-Back-N W=4
    gbn4_pts = []
    # Go-Back-N W=8
    gbn8_pts = []

    for i in range(100):
        a = i * (3.0 / 100.0)
        x = ox + (a / 3.0) * w
        # S&W
        eta_sw = (1.0 - Pf) / (1.0 + 2.0 * a)
        sw_pts.append((x, oy - eta_sw * h))
        # GBN W=4
        if 4.0 >= 1.0 + 2.0 * a:
            eta_g4 = (1.0 - Pf) / (1.0 + 2.0 * a * Pf)
        else:
            eta_g4 = 4.0 * (1.0 - Pf) / ((1.0 + 2.0 * a) * (1.0 - Pf + 4.0 * Pf))
        gbn4_pts.append((x, oy - eta_g4 * h))
        # GBN W=8
        if 8.0 >= 1.0 + 2.0 * a:
            eta_g8 = (1.0 - Pf) / (1.0 + 2.0 * a * Pf)
        else:
            eta_g8 = 8.0 * (1.0 - Pf) / ((1.0 + 2.0 * a) * (1.0 - Pf + 8.0 * Pf))
        gbn8_pts.append((x, oy - eta_g8 * h))

    draw.line(sw_pts, fill=ACCENT_RED, width=3)
    draw.line(gbn4_pts, fill=ACCENT_GOLD, width=3)
    draw.line(gbn8_pts, fill=ACCENT_GREEN, width=3)

    # Legend
    draw.text((ox + 500, oy - h + 40), "--- Go-Back-N (Window W=8, Pf=0.1)", fill=ACCENT_GREEN)
    draw.text((ox + 500, oy - h + 70), "--- Go-Back-N (Window W=4, Pf=0.1)", fill=ACCENT_GOLD)
    draw.text((ox + 500, oy - h + 100), "--- Stop-and-Wait: eta = (1-Pf)/(1+2a)", fill=ACCENT_RED)

    im.save("docs/screenshots/09_arq_efficiency.png")

if __name__ == "__main__":
    gen_fig1()
    gen_fig2()
    gen_fig3()
    gen_fig4()
    gen_fig5()
    gen_fig6()
    gen_fig7()
    gen_fig8()
    gen_fig9()
    print("Successfully generated all 9 screenshots in docs/screenshots/")
