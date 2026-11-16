"""Synthwave score for the trailer: 120 BPM, A minor, 22 s. Writes music.wav (48 kHz stereo float)."""
import os
import numpy as np
from scipy.signal import butter, lfilter, sosfilt, fftconvolve
import scipy.io.wavfile as wavfile

SR = 48000
BPM = 120.0
BEAT = 60.0 / BPM
BAR = 4 * BEAT
TOTAL = 22.0
N = int(TOTAL * SR)
rng = np.random.default_rng(1987)

out = np.zeros((N, 2))


def note_hz(name):
    names = {'C': -9, 'C#': -8, 'D': -7, 'D#': -6, 'E': -5, 'F': -4, 'F#': -3, 'G': -2, 'G#': -1, 'A': 0, 'A#': 1, 'B': 2}
    n, o = name[:-1], int(name[-1])
    return 440.0 * 2 ** ((names[n] + (o - 4) * 12) / 12)


def saw(f, n, detune_cents=0.0, phase=0.0):
    f = f * 2 ** (detune_cents / 1200)
    t = np.arange(n) / SR
    K = int(min(60, (SR / 2) / f))
    y = np.zeros(n)
    for k in range(1, K + 1):
        y += np.sin(2 * np.pi * k * f * t + phase * k) / k
    return y * (2 / np.pi)


def square(f, n, duty=0.5):
    t = np.arange(n) / SR
    return np.where((t * f) % 1.0 < duty, 1.0, -1.0)


def env_adsr(n, a=0.005, d=0.1, s=0.7, r=0.05):
    e = np.ones(n) * s
    na, nd, nr = int(a * SR), int(d * SR), int(r * SR)
    na = min(na, n)
    e[:na] = np.linspace(0, 1, na, endpoint=False)
    if na < n:
        m = min(nd, n - na)
        e[na:na + m] = np.linspace(1, s, m, endpoint=False)
    if nr > 0 and n > nr:
        e[-nr:] *= np.linspace(1, 0, nr)
    return e


def lowpass(x, fc, order=2):
    sos = butter(order, min(fc, SR * 0.45), 'low', fs=SR, output='sos')
    return sosfilt(sos, x)


def highpass(x, fc, order=2):
    sos = butter(order, fc, 'high', fs=SR, output='sos')
    return sosfilt(sos, x)


def add(x, t, gain=1.0, pan=0.0):
    i = int(t * SR)
    if i >= N:
        return
    x = x[: N - i]
    l, r = np.sqrt(0.5 * (1 - pan)), np.sqrt(0.5 * (1 + pan))
    if x.ndim == 1:
        out[i:i + len(x), 0] += x * gain * l
        out[i:i + len(x), 1] += x * gain * r
    else:
        out[i:i + len(x)] += x * gain


def reverb_ir(length, decay, gate=None):
    n = int(length * SR)
    ir = rng.standard_normal((n, 2)) * np.exp(-np.arange(n) / (decay * SR))[:, None]
    if gate:
        g = int(gate * SR)
        ir[g:] = 0
    ir[:, 0] = lowpass(ir[:, 0], 7000)
    ir[:, 1] = lowpass(ir[:, 1], 7000)
    return ir / np.sqrt((ir ** 2).sum(0)).max()


# ---- Drums ----------------------------------------------------------------
def kick():
    n = int(0.45 * SR)
    t = np.arange(n) / SR
    f = 45 + 120 * np.exp(-t * 28)
    ph = 2 * np.pi * np.cumsum(f) / SR
    y = np.sin(ph) * np.exp(-t * 7)
    y += 0.4 * rng.standard_normal(n) * np.exp(-t * 300)
    return np.tanh(y * 1.8)


def snare():
    n = int(0.25 * SR)
    t = np.arange(n) / SR
    noise = highpass(rng.standard_normal(n), 900) * np.exp(-t * 16)
    tone = np.sin(2 * np.pi * 190 * t) * np.exp(-t * 30)
    return noise * 0.8 + tone * 0.6


def hat(open_=False):
    n = int((0.25 if open_ else 0.05) * SR)
    t = np.arange(n) / SR
    return highpass(rng.standard_normal(n), 7000) * np.exp(-t * (14 if open_ else 90))


def crash():
    n = int(2.5 * SR)
    t = np.arange(n) / SR
    return highpass(rng.standard_normal(n), 4000) * np.exp(-t * 1.6)


K, S, HC, HO, CR = kick(), snare(), hat(), hat(True), crash()
snare_bus = np.zeros((N, 2))


def snare_at(t, g=1.0):
    i = int(t * SR)
    x = S[: N - i]
    snare_bus[i:i + len(x), 0] += x * g
    snare_bus[i:i + len(x), 1] += x * g


# ---- Arrangement -----------------------------------------------------------
CHORDS = [('A', ['A3', 'C4', 'E4', 'A4']), ('F', ['F3', 'A3', 'C4', 'F4']), ('C', ['C4', 'E4', 'G4', 'C5']), ('G', ['G3', 'B3', 'D4', 'G4'])]
BASS = {'A': 'A1', 'F': 'F1', 'C': 'C2', 'G': 'G1'}
DROP = 2 * BAR        # 4.0 s: the gameplay starts
LOGO = 7.5 * BAR      # 15.0 s: the title


def pad(chord, t, dur, gain):
    n = int(dur * SR)
    y = np.zeros((n, 2))
    for i, nm in enumerate(chord):
        f = note_hz(nm)
        for d, p in ((-9, -0.6), (0, 0.0), (9, 0.6)):
            v = saw(f, n, d, phase=rng.uniform(0, 6.28))
            y[:, 0] += v * np.sqrt(0.5 * (1 - p))
            y[:, 1] += v * np.sqrt(0.5 * (1 + p))
    e = env_adsr(n, a=0.25, d=0.3, s=0.85, r=0.3)[:, None]
    y[:, 0] = lowpass(y[:, 0], 1800)
    y[:, 1] = lowpass(y[:, 1], 1800)
    add(y * e * gain / 6, t)


def arp(chord, t0, bars, gain, cutoff0, cutoff1):
    step = BEAT / 4
    notes = chord + chord[::-1][1:3]
    steps = int(bars * 16)
    for s in range(steps):
        nm = notes[s % len(notes)]
        f = note_hz(nm) * 2
        n = int(step * 0.95 * SR)
        y = square(f, n, 0.3) * env_adsr(n, 0.002, 0.08, 0.3, 0.02)
        fc = cutoff0 + (cutoff1 - cutoff0) * s / max(1, steps - 1)
        y = lowpass(y, fc)
        add(y * gain, t0 + s * step, pan=0.35 * np.sin(s * 0.7))


def bass(root, t0, bars, gain):
    step = BEAT / 2
    f = note_hz(BASS[root])
    for s in range(int(bars * 8)):
        n = int(step * 0.9 * SR)
        ff = f * (2 if s % 2 else 1)
        y = saw(ff, n) + 0.5 * square(ff * 0.5, n, 0.5)
        e = env_adsr(n, 0.003, 0.12, 0.5, 0.02)
        y = lowpass(y * e, 900 + 900 * (s % 2))
        add(np.tanh(y * 1.5) * gain, t0 + s * step)


LEAD = [  # (beat offset within 4 bars, note, length in beats)
    (0, 'A4', 1), (1.5, 'C5', 0.5), (2, 'E5', 1), (3, 'D5', 0.5), (3.5, 'C5', 0.5),
    (4, 'F4', 1), (5.5, 'A4', 0.5), (6, 'C5', 1), (7, 'E5', 0.5), (7.5, 'D5', 0.5),
    (8, 'E5', 1), (9.5, 'G5', 0.5), (10, 'E5', 0.5), (10.5, 'D5', 0.5), (11, 'C5', 0.5), (11.5, 'D5', 0.5),
    (12, 'D5', 1), (13.5, 'B4', 0.5), (14, 'G4', 0.5), (14.5, 'A4', 1.5),
]


def lead_note(nm, t, beats, gain):
    n = int(beats * BEAT * SR)
    f = note_hz(nm)
    tt = np.arange(n) / SR
    vib = 1 + 0.004 * np.sin(2 * np.pi * 5.5 * tt) * np.clip(tt * 3, 0, 1)
    y = np.zeros((n, 2))
    for d, p in ((-12, -0.5), (0, 0), (12, 0.5)):
        ph = 2 * np.pi * np.cumsum(f * 2 ** (d / 1200) * vib) / SR
        v = sum(np.sin(k * ph) / k for k in range(1, 18)) * (2 / np.pi)
        y[:, 0] += v * np.sqrt(0.5 * (1 - p))
        y[:, 1] += v * np.sqrt(0.5 * (1 + p))
    env = env_adsr(n, 0.01, 0.15, 0.75, 0.06)
    fenv = 900 + 4000 * np.exp(-tt * 6)
    # brassy: filter sweep approximated in two bands
    lo = np.stack([lowpass(y[:, c], 1200) for c in range(2)], 1)
    hi = y - lo
    y = lo + hi * (fenv / 4900)[:, None]
    add(y * env[:, None] * gain / 3, t)


# intro: pad + arp opening up + riser
pad(CHORDS[0][1], 0.0, BAR, 0.55)
pad(CHORDS[1][1], BAR, BAR, 0.55)
arp(CHORDS[0][1], 0.0, 1, 0.10, 500, 1500)
arp(CHORDS[1][1], BAR, 1, 0.12, 1500, 5000)
nr = int(BAR * SR)
tt = np.arange(nr) / SR
riser = rng.standard_normal(nr)
riser = np.concatenate([lowpass(riser[i:i + 4800], 300 + 9000 * (i / nr) ** 2) for i in range(0, nr, 4800)])[:nr]
add(riser * (tt / BAR) ** 3 * 0.35, BAR)
for k in range(8):  # snare roll into the drop
    snare_at(DROP - BEAT * 2 + k * BEAT / 4, 0.25 + 0.08 * k)

# main section: 5.5 bars
bar_t = DROP
b = 0
while bar_t < LOGO - 1e-6:
    bars = min(1.0, (LOGO - bar_t) / BAR)
    name, chord = CHORDS[b % 4]
    pad(chord, bar_t, bars * BAR, 0.32)
    bass(name, bar_t, bars, 0.30)
    arp(chord, bar_t, bars, 0.07, 2500, 3500)
    for q in range(int(bars * 4)):
        add(K, bar_t + q * BEAT, 0.9)
        if q % 2 == 1:
            snare_at(bar_t + q * BEAT, 1.0)
        for h in range(4):
            add(HO if h == 2 else HC, bar_t + q * BEAT + h * BEAT / 4, 0.10 if h == 2 else 0.07, pan=0.3)
    bar_t += BAR
    b += 1
add(CR, DROP, 0.35, pan=-0.2)
add(K, DROP, 1.0)
for (off, nm, ln) in LEAD:
    tl = DROP + off * BEAT
    if tl < LOGO - 0.01:
        lead_note(nm, tl, min(ln, (LOGO - tl) / BEAT), 0.26)
for (off, nm, ln) in LEAD[:12]:  # second pass, an octave up for the last bars
    tl = DROP + 4 * BAR + off * BEAT
    if tl < LOGO - 0.01:
        lead_note(nm[:-1] + str(int(nm[-1]) + 1), tl, min(ln, (LOGO - tl) / BEAT), 0.16)

# logo: huge stab, boom, crash, sustained chord + last lead phrase
stab = ['A2', 'E3', 'A3', 'C4', 'E4', 'B4', 'A4']
pad(stab, LOGO, TOTAL - LOGO, 0.9)
n = int(3.0 * SR)
tt = np.arange(n) / SR
boom = np.sin(2 * np.pi * np.cumsum(30 + 60 * np.exp(-tt * 4)) / SR) * np.exp(-tt * 1.5)
add(np.tanh(boom * 2) * 0.8, LOGO)
add(CR, LOGO, 0.5, pan=0.2)
add(K, LOGO, 1.0)
snare_at(LOGO, 1.2)
for (nm, off, ln) in (('E5', 1, 1), ('D5', 2, 0.5), ('C5', 2.5, 0.5), ('A4', 3, 4)):
    lead_note(nm, LOGO + off * BEAT, ln, 0.22)
arp(['A3', 'C4', 'E4', 'B4'], LOGO + BAR, 2, 0.05, 3000, 600)

# gated reverb on the snare bus, a plate on everything
snare_wet = np.stack([fftconvolve(snare_bus[:, c], reverb_ir(0.6, 0.5, gate=0.28)[:, c])[:N] for c in range(2)], 1)
out += snare_bus * 0.55 + snare_wet * 0.9
ir = reverb_ir(2.5, 0.8)
wet = np.stack([fftconvolve(out[:, c], ir[:, c])[:N] for c in range(2)], 1)
out = out + wet * 0.18

# fade out, master
fade = int(1.2 * SR)
out[-fade:] *= np.linspace(1, 0, fade)[:, None]
out = np.tanh(out * 1.1)
out /= np.abs(out).max() / 0.9
wavfile.write(os.environ.get('TRAILER_DIR', '/tmp/trailer') + '/work/music.wav', SR, out.astype(np.float32))
print('music ok', out.shape)
