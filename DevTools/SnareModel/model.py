"""A first modelled snare: modal head/shell + stick pulse + contact-driven wires (not shipped; for listening)."""
import sys, json, numpy as np, soundfile as sf
from scipy.signal import find_peaks, butter, sosfilt, firwin2, fftconvolve

SR = 48000
ONSETS = [0.000, 2.003]          # the two long, undisturbed hits in each recording

def onset(m, t):
    i = int(t * SR); j = i + np.argmax(np.abs(m[i:i + 2400]) > 0.02 * np.abs(m[i:i + 2400]).max() * 5)
    return j

def stft(seg, win=4096, hop=256, n=1 << 16):
    frames = [np.abs(np.fft.rfft(seg[k:k + win] * np.hanning(win), n)) for k in range(0, len(seg) - win, hop)]
    return np.array(frames), np.fft.rfftfreq(n, 1 / SR), (np.arange(len(frames)) * hop + win / 2) / SR

def fit(path, max_modes=34):
    x, sr = sf.read(path); assert sr == SR
    m = x.mean(axis=1)
    hits = [m[onset(m, t):onset(m, t) + int(1.9 * SR)] for t in ONSETS]
    S = np.mean([stft(h)[0] for h in hits], axis=0); _, fr, tt = stft(hits[0])
    early = 20 * np.log10(S[(tt > 0.035) & (tt < 0.2)].mean(axis=0) + 1e-12)
    band = (fr > 90) & (fr < 5000)
    pk, _ = find_peaks(early[band], prominence=4, distance=int(5 / fr[1]))
    cand = sorted([(early[band][p], fr[band][p]) for p in pk], reverse=True)[:max_modes]
    modes = []
    for _, f0 in sorted(cand, key=lambda c: c[1]):
        k = np.argmin(np.abs(fr - f0))
        # refine the peak (parabolic) on the early spectrum
        a, b, c = early[k - 1:k + 2]; off = 0.5 * (a - c) / (a - 2 * b + c) if (a - 2 * b + c) != 0 else 0
        f = f0 + off * fr[1]
        lvl = 20 * np.log10(S[:, k - 2:k + 3].max(axis=1) + 1e-12)
        sel = tt > 0.035; y = lvl[sel]; xx = tt[sel]
        stop = np.where(y < y[:8].max() - 30)[0]; end = max(stop[0] if len(stop) else len(y), 8)
        slope, icpt = np.polyfit(xx[:end], y[:end], 1)
        t60 = float(np.clip(-60 / slope if slope < 0 else 3.0, 0.05, 3.0))
        modes.append({'f': float(f), 't60': t60, 'level0': float(icpt)})   # level at t = 0 (dB, relative)
    top = max(md['level0'] for md in modes)
    for md in modes: md['level0'] -= top
    # the wires: their colour (the hiss above the rings, 30-130 ms) and how their buzz falls
    seg = np.concatenate([h[int(0.03 * SR):int(0.13 * SR)] for h in hits])
    n = 1 << 14; spec = np.abs(np.fft.rfft(seg[:n] * np.hanning(min(n, len(seg))) if len(seg) >= n else np.pad(seg, (0, n - len(seg))), n))
    fq = np.fft.rfftfreq(n, 1 / SR)
    sm = np.exp(np.convolve(np.log(spec + 1e-9), np.ones(41) / 41, 'same'))
    grid = np.linspace(0, SR / 2, 257); colour = np.interp(grid, fq, sm)
    colour[grid < 1500] *= (grid[grid < 1500] / 1500) ** 4
    colour /= colour.max()
    sos = butter(4, [2500, 14000], 'band', fs=SR, output='sos')
    wire_env = []
    for h in hits:
        y = sosfilt(sos, h); e = np.sqrt(np.convolve(y * y, np.ones(96) / 96, 'same')); wire_env.append(e[:int(0.6 * SR)])
    wire_env = np.mean(wire_env, axis=0)
    return {'modes': modes, 'wireColour': colour.tolist(), 'wireEnvRef': (wire_env / wire_env.max())[::48].tolist(),
            'peak': float(np.mean([np.abs(h).max() for h in hits]))}

def render(p, velocity=0.8, length=1.9, seed=1, position=0.0, wires=1.0):
    """velocity 0..1; position 0 = centre, 1 = near the edge."""
    rng = np.random.default_rng(seed)
    n = int(length * SR); t = np.arange(n) / SR
    # the stick: a raised-cosine force pulse - harder = shorter (brighter) and stronger
    dur = 0.0016 - 0.0008 * velocity
    k = np.arange(int(dur * SR) + 1); pulse = np.sin(np.pi * k / (len(k) - 1)) ** 2; pulse /= pulse.sum()
    force = velocity ** 1.6
    # pitch: hard hits stretch the head - a brief lift that settles in ~25 ms
    lift = 1.0 + 0.012 * velocity ** 2 * np.exp(-t / 0.025)
    head = np.zeros(n); low = np.zeros(n)
    for md in p['modes']:
        amp = 10 ** (md['level0'] / 20)
        # near the edge: the low rings weaker, the upper ones relatively stronger
        if position > 0: amp *= (1 - 0.6 * position) if md['f'] < 400 else (1 + 0.5 * position)
        phase = 2 * np.pi * np.cumsum(md['f'] * lift) / SR + rng.uniform(-0.2, 0.2)
        ring = amp * np.exp(-6.9078 * t / md['t60']) * np.sin(phase)
        head += ring
        if md['f'] < 450: low += ring       # what the bottom head is doing - what drives the wires
    head = fftconvolve(head, pulse)[:n] * force
    low = fftconvolve(low, pulse)[:n] * force
    # the wires: they slap only while the bottom head moves more than a little - so the buzz follows the drum
    drive = np.abs(low); drive = np.convolve(drive, np.ones(48) / 48, 'same')
    ref = drive.max() + 1e-12; threshold = 0.035 * ref
    contact = np.clip(drive - threshold, 0, None) / ref
    contact = contact ** 1.15
    colour = np.array(p['wireColour']); taps = firwin2(511, np.linspace(0, 1, len(colour)), colour)
    noise = fftconvolve(rng.standard_normal(n), taps)[:n]
    noise *= 1 + 0.5 * np.sign(np.sin(2 * np.pi * np.cumsum(np.full(n, 1.0)) * 0))  # (placeholder: no extra rattle)
    buzz = noise * contact * p.get('wireGain', 1.0) * wires
    # the stick's own tick on the coated head: a very short papery click
    click_n = int(0.003 * SR); click = rng.standard_normal(click_n) * np.hanning(click_n) * 0.15 * velocity ** 2
    out = head + buzz; out[:click_n] += sosfilt(butter(2, [1500, 9000], 'band', fs=SR, output='sos'), click) * force
    return out

if __name__ == '__main__':
    path, out = sys.argv[1], sys.argv[2]
    p = fit(path); json.dump(p, open(out, 'w'), indent=1)
    print('%d modes; strongest:' % len(p['modes']), ', '.join('%.0f Hz %.2f s %+.0f dB' % (m['f'], m['t60'], m['level0']) for m in sorted(p['modes'], key=lambda m: -m['level0'])[:8]))
