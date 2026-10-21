import numpy as np
exec(open('proto.py').read().split("if __name__")[0])

def onsets(x, min_gap_ms=80):
    mono = np.max(np.abs(x), axis=1) if x.ndim > 1 else np.abs(x)
    # fast and slow envelopes in dB; onset when fast exceeds slow by 9 dB
    def env(p, ms):
        c = np.exp(-1/(ms*1e-3*SR)); y = np.empty_like(p); s = 0.0
        for i, v in enumerate(p):
            s = v if v > s else v + c*(s - v); y[i] = s
        return y
    f = 20*np.log10(env(mono, 2) + 1e-6); s = 20*np.log10(env(mono, 60) + 1e-6)
    s = np.concatenate([np.full(240, -120.0), s[:-240]])   # slow envelope 5 ms ago
    idx = []; last = -10**9
    for i in np.nonzero(f - s > 9)[0]:
        if i - last > min_gap_ms*1e-3*SR:
            idx.append(i); last = i
    return np.array(idx)

def report(name, T, R, att, rel, variants):
    x = load(name, 8.0)
    L = peak_level_db(x)
    on = onsets(x)
    on = on[on + 4800 < len(L)]
    print(f'{name}: T {T} R {R} att {att} rel {rel}  ({len(on)} onsets)')
    mono = x.mean(axis=1) if x.ndim > 1 else x
    for label, fn in variants:
        g = fn(L, T, R, att, rel)
        y = mono * 10 ** (-g / 20)
        crest_out = 20*np.log10(np.max(np.abs(y))/np.sqrt(np.mean(y**2)))
        prof = ''
        if len(on):
            prof = ' GR after onset ' + ' '.join(f'{ms}ms:{np.mean(g[on + int(ms*48)]):4.1f}' for ms in (0, 2, 5, 10, 30, 80))
        print(f'   {label:12s} avg {np.mean(g):5.2f} max {np.max(g):5.2f} p90 {np.percentile(g,90):5.2f} crest {crest_out:5.1f}{prof}')

V = [('modern', modern),
     ('vint a=0', lambda L,T,R,a,r: vintage(L,T,R,a,r, alpha=0.0)),
     ('vint a=.5', lambda L,T,R,a,r: vintage(L,T,R,a,r, alpha=0.5)),
     ('vint a=1', lambda L,T,R,a,r: vintage(L,T,R,a,r, alpha=1.0))]
for args in [('drumbus_rock', -24, 4, 10, 100), ('drumbus_rock', -24, 4, 30, 200), ('snare_rock', -24, 4, 10, 100), ('bass_picked', -26, 4, 10, 100), ('vocal_rock', -24, 3, 5, 120), ('mix_song', -16, 2, 30, 200)]:
    report(*args, V)

t = np.arange(int(2*SR))/SR
print('steady sine 12 dB over, R4 10/100: mean GR (ripple p-p)  [static 9.0]')
for f in (50.0, 100.0, 1000.0):
    x = 10**((-20+12)/20)*np.sin(2*np.pi*f*t)
    L = peak_level_db(x)
    print(f'  {f:5.0f} Hz ' + '  '.join(f'{lab} {np.mean(g[-4800:]):5.2f} ({np.ptp(g[-4800:]):4.2f})' for lab, g in [(l, fn(L, -20, 4, 10, 100)) for l, fn in V]))
