import numpy as np
f32 = np.float32
L = np.load('/home/claude/opt/fit/log2_deg7.npy')[1:]   # c1..c7
E = np.load('/home/claude/opt/fit/exp2_deg5.npy')[1:]   # c1..c5
Lc = L.astype(np.float32); Ec = E.astype(np.float32)
print('log2 coeffs', [float(c) for c in Lc]); print('exp2 coeffs', [float(c) for c in Ec])

def flog2(x):
    x = x.astype(np.float32)
    i = x.view(np.int32)
    d = i - np.int32(0x3f3504f3)
    e = d >> 23
    m = (i - (e << 23)).view(np.float32)
    t = m - f32(1)
    p = Lc[6]
    for c in Lc[5::-1]:
        p = (p * t + c).astype(np.float32)
    p = (p * t).astype(np.float32)
    return (e.astype(np.float32) + p).astype(np.float32)

def fexp2(x):
    x = x.astype(np.float32)
    sh = f32(12582912.0)
    r = (x + sh).astype(np.float32)
    k = r.view(np.int32) - sh.view(np.int32)
    f = (x - (r - sh)).astype(np.float32)
    q = Ec[4]
    for c in Ec[3::-1]:
        q = (q * f + c).astype(np.float32)
    q = (q * f + f32(1)).astype(np.float32)
    return (q.view(np.int32) + (k << 23)).view(np.float32)

# exhaustive over all float32 in [0.5, 2): covers every mantissa on both sides of the reduction
lo = np.float32(0.5).view(np.int32); hi = np.float32(2.0).view(np.int32)
xs = np.arange(lo, hi, dtype=np.int32).view(np.float32)
err = np.abs(flog2(xs).astype(np.float64) - np.log2(xs.astype(np.float64)))
print('log2 on [0.5,2): max abs err %.3g (in dB %.3g)' % (err.max(), err.max() * 6.0206))
for scale in (2.0**-20, 2.0**-60, 2.0**40):
    ys = (xs * np.float32(scale)).astype(np.float32)
    err = np.abs(flog2(ys).astype(np.float64) - np.log2(ys.astype(np.float64)))
    print('   scaled by 2^%d: max abs err %.3g' % (int(np.log2(scale)), err.max()))
print('log2(1) =', flog2(np.array([1.0]))[0], ' log2(2)=', flog2(np.array([2.0]))[0], ' log2(0.5)=', flog2(np.array([0.5]))[0])

xs = np.linspace(-40, 40, 20000001).astype(np.float32)
y = fexp2(xs).astype(np.float64); ref = 2.0 ** xs.astype(np.float64)
rel = np.abs(y / ref - 1)
print('exp2 on [-40,40]: max rel err %.3g (in dB %.3g)' % (rel.max(), 8.6859 * rel.max()))
print('exp2(0) =', fexp2(np.array([0.0]))[0], ' exp2(1)=', fexp2(np.array([1.0]))[0], ' exp2(-1)=', fexp2(np.array([-1.0]))[0])
# monotonicity checks
xs = np.arange(np.float32(0.25).view(np.int32), np.float32(4.0).view(np.int32), dtype=np.int32).view(np.float32)
v = flog2(xs); print('log2 monotonic non-decreasing:', bool(np.all(np.diff(v) >= 0)), 'violations', int(np.sum(np.diff(v) < 0)))
xs = np.arange(np.float32(-3.0).view(np.int32), np.float32(-0.0).view(np.int32) - 0, -1, dtype=np.int32)
