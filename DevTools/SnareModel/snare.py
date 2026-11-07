"""The first modelled SD296 (not shipped): fitted rings + a stick + snare wires that slap only while the bottom head moves."""
import sys; sys.path.insert(0, '/home/claude/snare/tools')
import json, numpy as np, soundfile as sf
from scipy.signal import butter, sosfilt, sosfiltfilt, firwin2, fftconvolve
SR = 48000
TICK_EDGES = [250, 500, 1000, 2000, 4000, 8000, 16000, 23900]
import os
DRUM = os.environ.get('DRUM', 'SD296')
FITS = json.load(open('/home/claude/snare/fit/%s.json' % DRUM))

def coloured_noise(colour, n, rng):
    taps = firwin2(511, np.linspace(0, 1, len(colour)), colour)
    # (long enough noise that every output sample is fully filtered - no start-up silence)
    return fftconvolve(rng.standard_normal(n + len(taps) - 1), taps, 'valid')

def wire_drive(modes, n, velocity):
    t = np.arange(n) / SR; low = np.zeros(n)
    for m in modes:
        if m['f'] < 450:
            low += m['amp'] * np.exp(-6.9078 * t / m['t60']) * np.cos(2 * np.pi * m['f'] * t + m['phase'])
    d = np.abs(low * velocity); w = 96
    return np.convolve(d, np.ones(w) / w, 'same')

def render(damping='No_Dampening', velocity=0.8, wires=1.0, seed=1, length=1.9, threshold=None, power=None, settle=None, slap=None, click=True):
    p = FITS[damping]; rng = np.random.default_rng(seed)
    n = int(length * SR); t = np.arange(n) / SR
    v_ref = 0.8   # the recorded hits: medium-hard
    # the rings: their measured response to the recorded hit, scaled for this hit -
    # softer = quieter and darker (a longer stick contact), harder = louder and brighter
    tilt = velocity - v_ref
    lift = 1.0 + 0.010 * (velocity ** 2 - v_ref ** 2) * np.exp(-t / 0.03)
    head = np.zeros(n)
    for m in p['modes']:
        if m['t60'] < 0.03: continue   # (shorter than 30 ms: the stick's impact, not a ring - the tick carries it)
        g = (velocity / v_ref) ** 1.5 * np.exp(tilt * 0.9 * (m['f'] / 1000.0))
        ph = 2 * np.pi * np.cumsum(m['f'] * lift) / SR + m['phase']
        # (the fit is only trusted from 3 ms on: during the stick contact each ring holds its 3 ms strength, rising in)
        head += g * m['amp'] * np.exp(-6.9078 * np.maximum(t, 0.003) / m['t60']) * np.cos(ph)
    # (the rings build up while the stick is on the head - about 3 ms)
    ramp = np.where(t < 0.003, np.sin(0.5 * np.pi * np.clip(t / 0.003, 0, 1)) ** 2, 1.0)
    head *= ramp
    # the wires: a plateau while the drum is loud, then a smooth fade - pulsing as the snare-side head swings into them
    w = p['wires']
    level = (velocity / v_ref) ** 1.4
    rate = w['rate'] * (v_ref / max(velocity, 0.05)) ** 0.5          # softer: the wires drop away sooner
    hold = w['hold'] * min(1.0, velocity / v_ref)
    edb = np.where(t < hold, w['plateau'], w['plateau'] - rate * (t - hold))
    edb = np.where(t < 0.002, w['plateau'] + 20 * np.log10(np.clip(t / 0.002, 1e-4, 1)), edb)
    env = 10 ** (edb / 20) * level
    pulse = 1 + w['depth'] * np.cos(2 * np.pi * w['pulseHz'] * t + rng.uniform(0, 6.28))
    buzz = coloured_noise(p['wireColour'], n, rng) * env * pulse * wires
    # the stick's own tick on the coated head
    cn = int(0.004 * SR)
    if 'tickBands' in p:
        edges = TICK_EDGES; tc = np.zeros(257); fq = np.linspace(0, SR / 2, 257)
        for (lo, hi), gain in zip(zip(edges[:-1], edges[1:]), p['tickBands']):
            tc[(fq >= lo) & (fq < hi)] = gain
        tc = np.convolve(tc, np.ones(5) / 5, 'same')
        tick = coloured_noise(tc / (tc.max() + 1e-12), cn, rng) * np.hanning(cn)
        tick *= (tc.max()) * (velocity / v_ref) ** 2.2
    else:
        tc = np.array(p['clickColour']); fq = np.linspace(0, SR / 2, len(tc))
        tc = tc * np.clip((fq - 1500.0) / 1500.0, 0, 1)
        tick = coloured_noise(tc / (tc.max() + 1e-12), cn, rng) * np.hanning(cn)
        tick *= p['clickRms'] / (np.sqrt((tick ** 2).mean()) + 1e-12) * (velocity / v_ref) ** 2.2 * p.get('tickGain', 1.0)
    out = head + buzz
    if click: out[:cn] += tick
    return out

def wire_envelope(y):
    hp = sosfiltfilt(butter(6, 2500, 'high', fs=SR, output='sos'), y)
    w = int(0.003 * SR); e = np.sqrt(np.convolve(hp * hp, np.ones(w) / w, 'same')); return e

def tune_wires(damping, pulse_hz, depth=0.45):
    """The wires' envelope from the recording: plateau level, how long it holds, how fast it fades (dB per second)."""
    p = FITS[damping]; target = np.array(p['wireEnv'])
    t = np.arange(len(target)) / 1000.0; sel = (t >= 0.008) & (t <= 0.6)
    y = 20 * np.log10(target[sel] + 1e-6); tt = t[sel]
    best = None
    for hold in np.arange(0.01, 0.12, 0.005):
        x = np.clip(tt - hold, 0, None)
        A = np.stack([np.ones_like(x), -x], axis=1)
        (plateau, rate), *_ = np.linalg.lstsq(A, y, rcond=None)
        err = np.sqrt(np.mean((A @ np.array([plateau, rate]) - y) ** 2))
        if best is None or err < best[0]: best = (err, hold, plateau, rate)
    err, hold, plateau, rate = best
    p['wires'] = {'hold': float(hold), 'plateau': 0.0, 'rate': float(rate), 'pulseHz': pulse_hz, 'depth': depth}
    # level: match the measured buzz (relative to the tick at its peak)
    base = render(damping, wires=0.0, click=False, length=0.62)
    yb = render(damping, click=False, length=0.62) - base
    e = wire_envelope(yb); ref = np.array(p['wireEnv'])
    k = np.arange(15, 120)
    gain_db = np.mean(20 * np.log10(ref[k] + 1e-6)) - np.mean(20 * np.log10(e[k * 48] / p['wireEnvMax'] + 1e-9))
    p['wires']['plateau'] = float(gain_db)
    # the stick's tick: as loud (above 2.5 kHz, in its first 4 ms) as the recording's
    p['tickGain'] = 1.0
    tick = render(damping, wires=0.0, length=0.62) - base
    tick_peak = wire_envelope(tick)[:int(0.005 * SR)].max()
    real_peak = np.array(p['wireEnv'])[1:4].max() * p['wireEnvMax']
    p['tickGain'] = float(real_peak / (tick_peak + 1e-12))
    return hold, rate, err

def tune_tick(damping):
    """The stick's tick, band by band: whatever the real attack (first 6 ms, two hits) has that the rings and wires don't."""
    import model
    p = FITS[damping]; p.pop('tickBands', None)
    x, sr = sf.read('/home/claude/snare/rec/%s_%s.wav' % (DRUM, damping)); m = x.mean(axis=1)
    reals = [m[model.onset(m, t0):model.onset(m, t0) + int(0.05 * SR)] for t0 in (0.0, 2.003)]
    bodies = [render(damping, click=False, seed=s, length=0.05) for s in (3, 4, 5)]
    def energy(y, lo, hi):
        z = sosfiltfilt(butter(4, [lo, min(hi, 23000)], 'band', fs=SR, output='sos'), y)[:int(0.006 * SR)]
        return (z ** 2).sum()
    # a unit tick's energy per band (white, so each band's share is known)
    probe = np.random.default_rng(0).standard_normal(int(0.004 * SR)) * np.hanning(int(0.004 * SR))
    probe = sosfiltfilt(butter(2, 30, 'high', fs=SR, output='sos'), probe)
    gains = []
    for lo, hi in zip(TICK_EDGES[:-1], TICK_EDGES[1:]):
        need = np.mean([energy(r, lo, hi) for r in reals]) - np.mean([energy(b, lo, hi) for b in bodies])
        unit = energy(np.pad(probe, (0, 2000)), lo, hi)
        gains.append(float(np.sqrt(max(need, 0.0) / unit)) if need > 0 else 0.0)
    p['tickBands'] = gains
    return gains

def pulse_of(damping):
    """How fast, and how strongly, the recorded buzz pulses (the snare-side head swinging into the wires)."""
    import model
    x, sr = sf.read('/home/claude/snare/rec/%s_%s.wav' % (DRUM, damping)); m = x.mean(axis=1)
    j = model.onset(m, 0.0); hit = m[j:j + int(0.6 * SR)]
    hp = sosfiltfilt(butter(6, 3000, 'high', fs=SR, output='sos'), hit)
    env = sosfiltfilt(butter(4, 1200, 'low', fs=SR, output='sos'), np.abs(hp))
    seg = env[int(0.02 * SR):int(0.2 * SR)]
    seg = seg / np.convolve(seg, np.ones(480) / 480, 'same') - 1
    sp = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), 1 << 16)); fq = np.fft.rfftfreq(1 << 16, 1 / SR)
    band = (fq > 80) & (fq < 1000)
    hz = float(fq[band][np.argmax(sp[band])])
    depth = float(np.clip(np.sqrt((seg ** 2).mean()) * np.sqrt(2), 0.1, 0.7))
    return hz, depth

if __name__ == '__main__':
    for dmp in FITS:
        hz, depth = pulse_of(dmp)
        hold, rate, err = tune_wires(dmp, hz, depth)
        print('%-15s wires: plateau for %.0f ms, then fading %.0f dB per second, pulsing at %.0f Hz (shape within %.1f dB rms)' % (dmp, hold * 1000, rate, hz, err))
        tune_tick(dmp)
    json.dump(FITS, open('/home/claude/snare/fit/%s.json' % DRUM, 'w'))
