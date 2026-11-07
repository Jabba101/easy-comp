"""The first modelled SD296 (not shipped): fitted rings + a stick + snare wires that slap only while the bottom head moves."""
import sys; sys.path.insert(0, '/home/claude/snare/tools')
import json, numpy as np, soundfile as sf
from scipy.signal import butter, sosfilt, sosfiltfilt, firwin2, fftconvolve
SR = 48000
FITS = json.load(open('/home/claude/snare/fit/fits.json'))

def coloured_noise(colour, n, rng):
    taps = firwin2(511, np.linspace(0, 1, len(colour)), colour)
    return fftconvolve(rng.standard_normal(n), taps)[:n]

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
        g = (velocity / v_ref) ** 1.5 * np.exp(tilt * 0.9 * (m['f'] / 1000.0))
        ph = 2 * np.pi * np.cumsum(m['f'] * lift) / SR + m['phase']
        head += g * m['amp'] * np.exp(-6.9078 * t / m['t60']) * np.cos(ph)
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
    tc = np.array(p['clickColour']); fq = np.linspace(0, SR / 2, len(tc))
    tc = tc * np.clip((fq - 1500.0) / 1500.0, 0, 1)        # (the rings already carry the lows: the tick is only the papery top)
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

if __name__ == '__main__':
    for dmp, hz in (('No_Dampening', 166.0), ('Dampening_Gel', 540.0), ('Dampening_Ring', 165.0)):
        hold, rate, err = tune_wires(dmp, hz)
        print('%-15s wires: plateau for %.0f ms, then fading %.0f dB per second, pulsing at %.0f Hz (shape within %.1f dB rms)' % (dmp, hold * 1000, rate, hz, err))
    json.dump(FITS, open('/home/claude/snare/fit/fits.json', 'w'))
