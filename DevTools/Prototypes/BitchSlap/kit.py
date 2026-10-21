"""Real drums for the Bitch-Slap examples: the Virtuosity kit, every mic of each hit."""
import subprocess
import numpy as np

SR = 48000
V = '/home/claude/samples/virtuosity/flac'
MICS = ('kickmic', 'snaremic', 'oh', 'room')
rng = np.random.default_rng(1980)


def decode(path, channels):
    raw = subprocess.run(['ffmpeg', '-v', 'error', '-i', path, '-f', 'f32le', '-ac', str(channels), '-ar', str(SR), '-'],
                         capture_output=True, check=True).stdout
    x = np.frombuffer(raw, dtype=np.float32).astype(np.float64)
    return x.reshape(-1, channels)


_cache = {}


def hit(suffix, mic):
    """One hit as one mic heard it: stereo (close mics centred)."""
    key = (suffix, mic)
    if key not in _cache:
        x = decode(f'{V}/{mic}_{suffix}.flac', 2)
        _cache[key] = x
    return _cache[key]


class Track:
    def __init__(self, seconds):
        self.x = np.zeros((int(seconds * SR), 2))

    def add(self, sound, t, db=0.0):
        i = int(round(t * SR))
        n = min(len(sound), len(self.x) - i)
        if n > 0:
            self.x[i:i + n] += sound[:n] * 10 ** (db / 20)


def groove(mics, bars=4, bpm=100.0, toms=False, ghosts=True, start=0.25):
    """A rock beat: kick 1 & 3 (and the 'and' of 3), snare 2 & 4, closed hats on 8ths,
    a few snare ghost notes, a rimshot on the last backbeat, and (toms=True) a tom fill
    in bars 2 and 4. mics: which mics to mix (e.g. ('snaremic',) for a snare track)."""
    b = 60.0 / bpm
    T = Track(start + bars * 4 * b + 1.5)
    for bar in range(bars):
        t0 = start + bar * 4 * b
        fill = toms and bar % 2 == 1
        for beat in range(4):
            t = t0 + beat * b
            if beat in (0, 2) and not (fill and beat == 2):
                for m in mics:
                    T.add(hit(f'kick_snon_vl4_rr{rng.integers(1, 5)}', m), t, rng.normal(0, 0.7))
            if beat in (1, 3) and not (fill and beat == 3):
                name = 'snare_rimshot_vl12' if (bar == bars - 1 and beat == 3) else f'snare_center_vl{rng.choice([33, 35, 36])}'
                for m in mics:
                    T.add(hit(name, m), t, rng.normal(0, 0.6))
            if not (fill and beat >= 2):
                for half in (0, 1):
                    for m in mics:
                        T.add(hit(f'hh_closed_vl{3 - half}_rr{rng.integers(1, 4)}', m), t + half * b / 2, rng.normal(0, 1.0))
        if ghosts and not fill:
            for pos in (1.75, 3.25):                      # quiet ghost notes on the 'a' of 2 and the 'e' of 4
                for m in mics:
                    T.add(hit(f'snare_center_vl{rng.choice([10, 14])}', m), t0 + pos * b, rng.normal(0, 1.0))
        if not fill and bar % 2 == 0:
            for m in mics:
                T.add(hit(f'kick_snon_vl3_rr{rng.integers(1, 5)}', m), t0 + 2.5 * b, rng.normal(0, 0.7))
        if fill:                                          # 8 sixteenths over beats 3 and 4: high tom, low tom
            for k in range(8):
                name = f'{"htom" if k < 4 else "ltom"}_center_vl{rng.choice([14, 16])}'
                for m in mics:
                    T.add(hit(name, m), t0 + 2 * b + k * b / 4, rng.normal(0, 0.8))
    return T.x


def normalise(x, peak_db=-8.0):
    return x * 10 ** (peak_db / 20) / np.max(np.abs(x))


if __name__ == '__main__':
    import scipy.io.wavfile as wf
    snare = normalise(groove(('snaremic',)))
    full = normalise(groove(MICS, toms=True), -6.0)
    print(snare.shape, full.shape)
