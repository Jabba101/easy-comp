"""Bitch-Slap prototypes (not shipped).

A  'slammed room': the classic studio way - a big, dense room (input diffusers + a
   16-line feedback delay network with a long decay), heard only while a gate keyed
   by the hits is open. The room starts fresh for each hit (its memory is cleared as
   the gate closes), so one hit's tail never leaks into the next.
B  'burst': the 80s digital 'non-linear' way - every sound gets its own dense burst of
   reverb that stops dead after LENGTH (a convolution with a gated noise burst).
"""
import numpy as np
from scipy import signal

SR = 48000


# ------------------------------------------------------------------ shared pieces
def hpf(x, hz, order=2):
    return signal.sosfilt(signal.butter(order, hz, 'highpass', fs=SR, output='sos'), x, axis=0)


def lpf(x, hz, order=2):
    return signal.sosfilt(signal.butter(order, hz, 'lowpass', fs=SR, output='sos'), x, axis=0)


def allpass(x, delay, g):
    b = np.zeros(delay + 1); a = np.zeros(delay + 1)
    b[0], b[-1] = -g, 1.0
    a[0], a[-1] = 1.0, -g
    return signal.lfilter(b, a, x)


def hadamard(n):
    h = np.array([[1.0]])
    while len(h) < n:
        h = np.block([[h, h], [h, -h]])
    return h / np.sqrt(n)


# ------------------------------------------------------------------ A: the room
DELAYS = np.array([293, 337, 389, 443, 503, 571, 631, 701, 773, 853, 929, 1009, 1093, 1181, 1277, 1381])
DIFFUSERS = [(155, 0.75), (116, 0.75), (413, 0.625), (301, 0.625)]


def room(x, rt60=3.0, hf_ratio=0.35, resets=(), block=256):
    """Mono in -> stereo wet. rt60: decay (s) at low frequencies; hf_ratio: how much
    shorter the decay is at the top. resets: sample indices where the room's memory
    is cleared."""
    for d, g in DIFFUSERS:
        x = allpass(x, d, g)
    n = len(DELAYS)
    H = hadamard(n)
    gains = 10 ** (-3.0 * DELAYS / (rt60 * SR))
    # Jot's absorption filters: one-pole lowpass per line, so the top decays faster
    pole = np.log(10) / 4 * np.log10(gains) * (1 - 1 / hf_ratio ** 2)
    b_in = np.array([1, -1, 1, 1, -1, 1, -1, -1, 1, 1, -1, 1, -1, 1, 1, -1], float) / np.sqrt(n)
    cL = np.array([1, 1, -1, 1, 1, -1, 1, 1, -1, 1, -1, -1, 1, -1, 1, 1], float) / np.sqrt(n)
    cR = np.array([1, -1, -1, -1, 1, 1, 1, -1, -1, 1, 1, -1, -1, -1, -1, 1], float) / np.sqrt(n)
    size = int(DELAYS.max()) + block + 1
    lines = np.zeros((n, size))
    state = np.zeros(n)                   # the damping filters' memory
    pos = 0
    out = np.zeros((len(x), 2))
    resets = sorted(set(int(r) for r in resets))
    r_i = 0
    start = 0
    while start < len(x):
        end = min(start + block, len(x))
        if r_i < len(resets) and resets[r_i] < end:
            if resets[r_i] > start:
                end = resets[r_i]
            else:
                lines[:] = 0.0; state[:] = 0.0
                r_i += 1
                continue
        m = end - start
        idx = (pos + np.arange(m)) % size
        read = np.empty((n, m))
        for i in range(n):
            read[i] = lines[i, (idx - DELAYS[i]) % size]
        # damping: y = g(1-p) x + p y_prev, per line
        damped = np.empty_like(read)
        for i in range(n):
            y, zf = signal.lfilter([gains[i] * (1 - pole[i])], [1, -pole[i]], read[i], zi=[pole[i] * state[i]])
            damped[i] = y; state[i] = y[-1]
        feedback = H @ damped
        lines[:, idx] = feedback + np.outer(b_in, x[start:end])
        out[start:end, 0] = cL @ damped
        out[start:end, 1] = cR @ damped
        pos = (pos + m) % size
        start = end
    return out


def triggers(dry, sensitivity_db=10.0, memory_db_per_s=6.0, jump_db=6.0, min_gap_ms=30.0):
    """Where the hits are. A hit is a sudden jump in level (so each hit of a fast fill
    counts, even while the last one is still ringing) that comes up to within
    sensitivity_db of the loudest recent hit (so ghost notes and bleed don't)."""
    level = np.max(np.abs(dry), axis=1) if dry.ndim > 1 else np.abs(dry)
    fast_rel = np.exp(-1.0 / (0.003 * SR))
    slow_att, slow_rel = np.exp(-1.0 / (0.010 * SR)), np.exp(-1.0 / (0.100 * SR))
    mem = 10 ** (-memory_db_per_s / 20 / SR)
    jump = 10 ** (jump_db / 20)
    th_ratio = 10 ** (-sensitivity_db / 20)
    gap = int(min_gap_ms * 1e-3 * SR)
    fast = slow = ref = 0.0
    last = -gap
    hits = []
    for i, v in enumerate(level):
        fast = v if v > fast else fast * fast_rel
        slow = slow_att * slow + (1 - slow_att) * fast if fast > slow else slow * slow_rel
        ref = max(fast, ref * mem)
        if fast > jump * slow and fast >= ref * th_ratio and fast > 1e-4 and i - last >= gap:
            hits.append(i); last = i
    return np.array(hits)


def windows(hits, n, length_ms, slam_ms=12.0, open_ms=1.0):
    """The gate: open at each hit, hold until LENGTH - slam, then slam shut (raised
    cosine). A new hit while open starts the window again. Returns the gate (0..1) and
    where it finishes closing."""
    length = int(length_ms * 1e-3 * SR); slam = int(slam_ms * 1e-3 * SR); op = max(1, int(open_ms * 1e-3 * SR))
    g = np.zeros(n)
    closes = []
    for k, h in enumerate(hits):
        stop = h + length
        nxt = hits[k + 1] if k + 1 < len(hits) else n
        seg_end = min(stop, nxt, n)
        t = np.arange(h, seg_end)
        rise = np.clip((t - h) / op, 0, 1)
        fall = np.clip((stop - t) / slam, 0, 1)
        shape = np.minimum(rise, 0.5 - 0.5 * np.cos(np.pi * fall))
        g[h:seg_end] = np.maximum(g[h:seg_end], shape)
        if stop <= nxt:
            closes.append(min(stop, n))
    return g, np.array(closes)


EARLY = [[(53, 0.7), (97, 0.7), (151, 0.7), (211, 0.7)], [(61, 0.7), (89, 0.7), (163, 0.7), (199, 0.7)]]


def early(x, chains=EARLY):
    """The first few ms: two short diffuser chains (left, right), so the room is there
    the instant the hit is - no slow bloom."""
    out = []
    for chain in chains:
        y = x.copy()
        for d, g in chain:
            y = allpass(y, d, g)
        out.append(y)
    return np.stack(out, axis=1)


def slammed_room(dry, length_ms, rt60=3.0, sensitivity_db=10.0, crush=True, early_mix=0.25, predelay_ms=2.0,
                 tone_hz=(180.0, 9000.0)):
    """Prototype A: returns the stereo wet signal (unscaled), the hits and the gate."""
    mono = dry.mean(axis=1)
    hits = triggers(dry, sensitivity_db)
    gate, closes = windows(hits, len(mono), length_ms)
    pre = int(predelay_ms * 1e-3 * SR)
    feed = np.concatenate([np.zeros(pre), mono[:-pre]]) * (gate > 0)
    feed = lpf(hpf(feed, tone_hz[0]), tone_hz[1])
    wet = room(feed, rt60=rt60, resets=closes) + early_mix * early(feed)
    if crush:
        wet = squash(wet)
    return wet * gate[:, None], hits, gate


def squash(wet, threshold_db=-12.0, ratio=6.0, attack_ms=1.0, release_ms=120.0):
    """The 'crush': a fast, hard compressor on the room (relative to its own peak),
    which makes it bloom and hold up until the gate slams."""
    level = np.max(np.abs(wet), axis=1)
    peak = np.max(level) + 1e-12
    db = 20 * np.log10(level / peak + 1e-9)
    a, r = np.exp(-1 / (attack_ms * 1e-3 * SR)), np.exp(-1 / (release_ms * 1e-3 * SR))
    gr = np.zeros_like(db); g = 0.0
    over = np.maximum(0.0, db - threshold_db) * (1 - 1 / ratio)
    for i, t in enumerate(over):
        c = a if t > g else r
        g = t + c * (g - t)
        gr[i] = g
    return wet * (10 ** (-gr / 20))[:, None]


# ------------------------------------------------------------------ B: the burst
def burst_ir(length_ms, seed=3, slam_ms=12.0, attack_ms=2.0, sag_db=4.0, tone_hz=(180.0, 9000.0)):
    n = int(length_ms * 1e-3 * SR)
    t = np.arange(n) / SR
    rng = np.random.default_rng(seed)
    ir = rng.standard_normal((n, 2))
    env = 10 ** (-sag_db * t / (length_ms * 1e-3) / 20)
    env *= np.clip(t / (attack_ms * 1e-3), 0, 1)
    fall = np.clip((length_ms * 1e-3 - t) / (slam_ms * 1e-3), 0, 1)
    env *= 0.5 - 0.5 * np.cos(np.pi * fall)
    ir *= env[:, None]
    ir = lpf(hpf(ir, tone_hz[0]), tone_hz[1])
    return ir / np.sqrt(np.sum(ir ** 2, axis=0, keepdims=True))


def burst(dry, length_ms, predelay_ms=2.0):
    mono = dry.mean(axis=1)
    ir = burst_ir(length_ms)
    pre = int(predelay_ms * 1e-3 * SR)
    wet = np.stack([signal.fftconvolve(mono, ir[:, c])[:len(mono)] for c in range(2)], axis=1)
    return np.concatenate([np.zeros((pre, 2)), wet[:-pre]])
