import numpy as np
from scipy.optimize import linprog

def minimax(basis_powers, func, weight, lo, hi, n=4000, fixed=None):
    # Chebyshev-clustered grid
    k = np.arange(n)
    x = (lo + hi) / 2 + (hi - lo) / 2 * np.cos(np.pi * (k + 0.5) / n)
    x = np.sort(np.concatenate([x, [lo, hi]]))
    A = np.stack([x ** p for p in basis_powers], axis=1)
    target = func(x) - (fixed(x) if fixed else 0.0)
    w = weight(x)
    m = len(basis_powers)
    # variables: c (m), delta ; minimize delta
    # w*(A c - target) <= delta ; -w*(A c - target) <= delta
    Aub = np.vstack([np.hstack([w[:, None] * A, -np.ones((len(x), 1))]),
                     np.hstack([-w[:, None] * A, -np.ones((len(x), 1))])])
    bub = np.concatenate([w * target, -w * target])
    cobj = np.zeros(m + 1); cobj[-1] = 1
    res = linprog(cobj, A_ub=Aub, b_ub=bub, bounds=[(None, None)] * (m + 1), method='highs')
    return res.x[:-1], res.x[-1]

# ---- log2(1+t), t in [sqrt(.5)-1, sqrt(2)-1), p(t) = t*(c1 + c2 t + ...)
lo, hi = np.sqrt(0.5) - 1, np.sqrt(2) - 1
for d in range(5, 10):
    c, err = minimax(list(range(1, d + 1)), lambda t: np.log2(1 + t), lambda t: np.ones_like(t), lo, hi)
    print('log2 degree', d, 'max abs err %.3g' % err)

# ---- exp2(f), f in [-0.5, 0.5], q(f) = 1 + f*(c1 + c2 f + ...), relative error
for d in range(4, 8):
    c, err = minimax(list(range(1, d + 1)), lambda f: 2.0 ** f, lambda f: 1.0 / 2.0 ** f, -0.5, 0.5, fixed=lambda f: np.ones_like(f))
    print('exp2 degree', d, 'max rel err %.3g' % err, c)
