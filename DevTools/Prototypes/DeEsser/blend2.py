exec(open('blend.py').read().split("refs = {")[0])
def feats2(name, fromHz, slow_ms):
    sr, x = wavfile.read(f'{M}/{name}.wav'); x = x.mean(axis=1).astype(np.float64)
    lab = np.array(json.load(open(f'{M}/{name}.json'))['labels'])
    band = signal.sosfilt(signal.butter(4, fromHz, 'highpass', fs=SR, output='sos'), x)**2
    rest = signal.sosfilt(signal.butter(4, fromHz, 'lowpass', fs=SR, output='sos'), x)**2
    k = 24; n = len(x)//k
    bf = follow_dec(band[:n*k].reshape(n,k).max(axis=1), k); rf = follow_dec(rest[:n*k].reshape(n,k).max(axis=1), k)
    tt = (x**2)[:n*k].reshape(n,k).mean(axis=1)
    sl = smooth_dec(tt, k, slow_ms)
    per = F//k; m = min(len(bf)//per, len(lab))
    f = lambda y, fn: fn(y[:m*per].reshape(m, per), axis=1)
    return f(bf, np.max), f(rf, np.mean), f(sl, np.mean), lab[:m]
print(' '*22 + ''.join(f'{m[:13]:>14s}' for m,_ in mats) + '   vocal avg  drum avg')
for slow_ms in [150, 300, 600]:
    F2 = {nm: feats2(nm, fr, slow_ms) for nm, fr in mats}
    for w in [0.4, 0.5, 0.6, 0.7]:
        row = []; accs = []
        for name, fr in mats:
            B,R,S,L = F2[name]
            ref = R**w * S**(1-w)
            r = 10*np.log10((B+1e-20)/(ref+1e-12))
            acc, t = score(r, L); accs.append(acc)
            row.append(f'{100*acc:5.1f}@{t:5.1f}  ')
        print(f'{"slow %dms rest^%.1f" % (slow_ms, w):22s}' + ''.join(f'{c:>14s}' for c in row) + f'   {100*np.mean(accs[:7]):5.1f}    {100*np.mean(accs[7:10]):5.1f}')
