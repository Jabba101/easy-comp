import numpy as np
from scipy.optimize import linprog
exec(open('/home/claude/opt/fit/fit.py').read().split('# ---- log2')[0])

def check_log(c):
    t = np.linspace(np.sqrt(0.5) - 1, np.sqrt(2) - 1, 200001)
    p = sum(ci * t ** (i + 1) for i, ci in enumerate(c))
    return np.max(np.abs(p - np.log2(1 + t)))

def check_exp(c):
    f = np.linspace(-0.5, 0.5, 200001)
    q = 1 + sum(ci * f ** (i + 1) for i, ci in enumerate(c))
    return np.max(np.abs(q / 2 ** f - 1))

for d in range(5, 10):
    c, _ = minimax(list(range(1, d + 1)), lambda t: np.log2(1 + t), lambda t: np.ones_like(t), np.sqrt(0.5) - 1, np.sqrt(2) - 1)
    print('log2 deg', d, 'true max abs err %.3g' % check_log(c))
for d in range(3, 8):
    c, _ = minimax(list(range(1, d + 1)), lambda f: 2.0 ** f, lambda f: 1.0 / 2.0 ** f, -0.5, 0.5, fixed=lambda f: np.ones_like(f))
    print('exp2 deg', d, 'true max rel err %.3g' % check_exp(c))
