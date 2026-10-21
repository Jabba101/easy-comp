import numpy as np
import matplotlib; matplotlib.use('Agg')
import matplotlib.pyplot as plt
exec(open('proto.py').read().split("if __name__")[0])
exec(open('v2.py').read().split("V = [")[0].split("exec(open('proto.py')")[1].split("\n",1)[1])

def vintage2(levels, T, R, att, rel, Fa, Fr, knee_in=9.0):
    s = R - 1
    a, rl = coef(att * Fa(s)), coef(rel * Fr(s))
    ko = knee_in * 2 / (R + 1)
    r = 0.0
    out = np.empty_like(levels)
    for n, L in enumerate(levels):
        rising = gc(L - T - r, s, ko) > r
        r = solve_zdf(L, T, s, ko, a if rising else rl, r)
        out[n] = r
    return out

V = [('modern', modern),
     ('E 1/1', lambda L,T,R,a,r: vintage2(L,T,R,a,r, lambda s: 1, lambda s: 1)),
     ('A R/1', lambda L,T,R,a,r: vintage2(L,T,R,a,r, lambda s: 1+s, lambda s: 1)),
     ('B R/sqR', lambda L,T,R,a,r: vintage2(L,T,R,a,r, lambda s: 1+s, lambda s: np.sqrt(1+s))),
     ('D R/R', lambda L,T,R,a,r: vintage2(L,T,R,a,r, lambda s: 1+s, lambda s: 1+s))]
if __name__ == '__main__':
    for args in [('drumbus_rock', -24, 4, 10, 100), ('drumbus_rock', -24, 4, 30, 200), ('snare_rock', -24, 4, 10, 100), ('bass_picked', -26, 4, 10, 100), ('vocal_rock', -24, 3, 5, 120), ('mix_song', -16, 2, 30, 200)]:
        report(*args, V)
    t = np.arange(int(2*SR))/SR
    print('steady sine 12 dB over, R4 10/100: mean GR (ripple p-p)  [static 9.0]')
    for f in (50.0, 100.0, 1000.0):
        x = 10**((-20+12)/20)*np.sin(2*np.pi*f*t)
        L = peak_level_db(x)
        print(f'  {f:5.0f} Hz ' + '  '.join(f'{lab} {np.mean(g[-4800:]):5.2f} ({np.ptp(g[-4800:]):4.2f})' for lab, g in [(l, fn(L, -20, 4, 10, 100)) for l, fn in V]))
    for name, T, R, a, r in [('drumbus_rock', -24, 4, 10, 100), ('bass_picked', -26, 4, 10, 100)]:
        x = load(name, 3.0); L = peak_level_db(x); tt = np.arange(len(L))/SR
        fig, ax = plt.subplots(2, 1, figsize=(14, 6), sharex=True)
        ax[0].plot(tt, L, color='0.6', lw=0.5); ax[0].axhline(T, color='k', ls='--', lw=0.8)
        for lab, fn in V:
            ax[1].plot(tt, -fn(L, T, R, a, r), lw=1.0, label=lab)
        ax[1].legend(); ax[0].set_title(f'{name} T{T} R{R} {a}/{r}')
        ax[1].set_xlim(0.5, 2.0)
        plt.tight_layout(); plt.savefig(f'v3_{name}.png', dpi=60); plt.close()
