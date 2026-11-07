"""The room as a set of resonances (peaking filters): take them out for the dry drum, put them back (any amount) for the room."""
import json, numpy as np
from scipy.signal import sosfilt
SR = 48000
scan = json.load(open('/home/claude/snare/fit/room_scan.json'))
fr = np.array(scan['freq']); avg = np.array(scan['avg']); agree = np.array(scan['agree'])

def peaking(f, gain_db, q):
    A = 10 ** (gain_db / 40); w = 2 * np.pi * f / SR; al = np.sin(w) / (2 * q)
    b = [1 + al * A, -2 * np.cos(w), 1 - al * A]; a = [1 + al / A, -2 * np.cos(w), 1 - al / A]
    return np.array(b + a) / a[0]

def resonances():
    out = []
    for f, h, g in scan['peaks']:
        if g < 4 / 6 or h < 3.0: continue                   # shared by at least 4 of the 6 drums, and clearly raised
        k = np.argmin(np.abs(fr - f)); half = h - 3.0
        lo = k
        while lo > 0 and avg[lo] > half: lo -= 1
        hi = k
        while hi < len(avg) - 1 and avg[hi] > half: hi += 1
        width = max(fr[hi] - fr[lo], 1.0)
        weight = 1.0 if g >= 5 / 6 else 0.7                 # the ones every drum shows count fully
        out.append({'f': float(f), 'gain': float(h * weight), 'q': float(np.clip(f / width, 2.0, 40.0))})
    return out

ROOM = resonances()

def apply(y, amount):
    """amount 1 = as recorded (the room in), 0 = the dry drum (the room out), anything between or beyond."""
    for r in ROOM:
        g = (amount - 1.0) * r['gain']
        if abs(g) > 0.01: y = sosfilt(peaking(r['f'], g, r['q'])[None, :], y)
    return y

def boost(f):
    """How much the room raises a pitch (not in dB): the product of its resonances at that frequency."""
    g = 1.0
    for r in ROOM:
        A = 10 ** (r['gain'] / 40); w0 = 2 * np.pi * r['f'] / SR; w = 2 * np.pi * f / SR
        c = peaking(r['f'], r['gain'], r['q']); b, a = c[:3], c[3:]
        z = np.exp(-1j * w * np.arange(3))
        g *= abs(np.dot(b, z) / np.dot(a, z))
    return g

def add(y, amount):
    """Put the room around a dry drum: amount 1 = your bedroom as recorded, 0 = none."""
    for r in ROOM:
        g = amount * r['gain']
        if abs(g) > 0.01: y = sosfilt(peaking(r['f'], g, r['q'])[None, :], y)
    return y

if __name__ == '__main__':
    print('the room:', ', '.join('%.0f Hz %+.1f dB Q%.0f' % (r['f'], r['gain'], r['q']) for r in ROOM))
    json.dump(ROOM, open('/home/claude/snare/fit/room.json', 'w'), indent=1)
