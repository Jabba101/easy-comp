import wave
import numpy as np
import kit, slap

SR = slap.SR


def write24(path, x):
    """24-bit WAV (Samplitude, and every player, read these)."""
    x = np.clip(x, -1.0, 1.0 - 2 ** -23)
    ints = np.round(x * (2 ** 23)).astype(np.int32)
    b = ints.astype('<i4').tobytes()
    frames = np.frombuffer(b, dtype=np.uint8).reshape(-1, 4)[:, :3].tobytes()
    with wave.open(path, 'wb') as w:
        w.setnchannels(x.shape[1]); w.setsampwidth(3); w.setframerate(SR)
        w.writeframes(frames)


def balance(dry, wet, gate, wet_vs_dry_db):
    """Scale the reverb so, while it's open, it sits wet_vs_dry_db relative to the dry sound."""
    on = gate > 0.5 if gate is not None else np.ones(len(dry), bool)
    rd = np.sqrt(np.mean(dry[on] ** 2)); rw = np.sqrt(np.mean(wet[on] ** 2)) + 1e-12
    return wet * (rd / rw) * 10 ** (wet_vs_dry_db / 20)
