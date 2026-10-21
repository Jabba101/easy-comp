import numpy as np
from numpy.polynomial import chebyshev as C, polynomial as P
from scipy.optimize import linprog

def lp_fit(func, weight, lo, hi, deg, zero_const=False, one_const=False, n=6000):
    k = np.arange(n)
    x = np.sort(np.concatenate([(lo+hi)/2 + (hi-lo)/2*np.cos(np.pi*(k+0.5)/n), [lo, hi]]))
    u = (2*x - (lo+hi)) / (hi-lo)                 # map to [-1,1]
    # monomial coefficients c_j (j=1..deg) times x^j, built from a chebyshev-scaled basis for conditioning:
    # represent p(x) = const + sum_j a_j * T_j(u) and enforce the constant via an equality on p(0)
    V = C.chebvander(u, deg)                      # columns T_0..T_deg
    f = func(x); w = weight(x)
    m = deg + 1
    Aub = np.vstack([np.hstack([w[:, None]*V, -np.ones((len(x), 1))]), np.hstack([-w[:, None]*V, -np.ones((len(x), 1))])])
    bub = np.concatenate([w*f, -w*f])
    u0 = (2*0 - (lo+hi)) / (hi-lo)
    Aeq = np.hstack([C.chebvander(np.array([u0]), deg), [[0.0]]])
    beq = [0.0 if zero_const else 1.0]
    cobj = np.zeros(m+1); cobj[-1] = 1
    res = linprog(cobj, A_ub=Aub, b_ub=bub, A_eq=Aeq, b_eq=beq, bounds=[(None, None)]*(m+1), method='highs')
    a = res.x[:-1]
    # convert chebyshev-in-u to monomial-in-x
    cheb_in_u = C.cheb2poly(a)                    # monomial in u
    # u = alpha*x + beta
    alpha, beta = 2/(hi-lo), -(lo+hi)/(hi-lo)
    poly_x = np.zeros(deg+1)
    for j, cj in enumerate(cheb_in_u):
        poly_x[:j+1] += cj * P.polypow([beta, alpha], j)[:j+1]
    return poly_x

lo, hi = np.sqrt(0.5) - 1, np.sqrt(2) - 1
for deg in (6, 7, 8):
    p = lp_fit(lambda t: np.log2(1+t), lambda t: np.ones_like(t), lo, hi, deg, zero_const=True)
    t = np.linspace(lo, hi, 400001)
    print('log2 deg', deg, 'err %.3g' % np.max(np.abs(P.polyval(t, p) - np.log2(1+t))), 'c0=%.2g' % p[0])
    np.save('log2_deg%d.npy' % deg, p)
for deg in (4, 5, 6):
    p = lp_fit(lambda f: 2.0**f, lambda f: 2.0**-f, -0.5, 0.5, deg, one_const=True)
    f = np.linspace(-0.5, 0.5, 400001)
    print('exp2 deg', deg, 'rel err %.3g' % np.max(np.abs(P.polyval(f, p)/2**f - 1)), 'c0=%r' % p[0])
    np.save('exp2_deg%d.npy' % deg, p)
