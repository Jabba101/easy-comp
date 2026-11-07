import sys, numpy as np, soundfile as sf
on = [0.000, 2.003, 4.000, 5.000, 6.000, 6.745, 7.250, 7.500]
files = sys.argv[1:]
data = {f.split('/')[-1]: sf.read(f) for f in files}
def seg(x, sr, t, ms=200):
    i = int(t*sr); i = max(0, i - 20)
    m = x[i:i+int(ms/1000*sr)].mean(axis=1)
    return m
def best(a, b):
    n = min(len(a), len(b)); a = a[:n]; b = b[:n]
    c = np.correlate(a, b, 'full'); k = np.argmax(np.abs(c))
    return c[k] / np.sqrt((a*a).sum() * (b*b).sum())
for name, (x, sr) in data.items():
    segs = [seg(x, sr, t, 150) for t in on[:7]]
    print(name, 'hit-vs-hit similarity (1.0 = identical copies):', ' '.join('%.3f' % best(segs[0], s) for s in segs[1:]))
names = list(data)
a = seg(*data[names[0]], 0.0); b = seg(*data[names[1]], 0.0); c = seg(*data[names[2]], 0.0)
print('first hit across files:', '%.3f %.3f' % (best(a, b), best(a, c)))
# lengths of ring: time to fall 40 dB after each hit
for name, (x, sr) in data.items():
    m = x.mean(axis=1); out = []
    for k, t in enumerate(on[:4]):
        i = int(t*sr); end = int((on[k+1] if k+1 < len(on) else 8.0)*sr)
        e = np.abs(m[i:end]); pk = e[:int(0.02*sr)].max()
        sm = np.convolve(e, np.ones(240)/240, 'same')
        below = np.where(sm < pk * 10**(-40/20))[0]
        out.append(below[0]/sr if len(below) else (end-i)/sr)
    print(name, 'time to fall 40 dB:', ' '.join('%.2f s' % v for v in out))
