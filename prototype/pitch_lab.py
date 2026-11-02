"""
Step 4 - Pitch detection lab.

Compares three ways of finding the pitch of a guitar note:
  A) FFT peak        (the "obvious" method - and why it fails)
  B) Autocorrelation
  C) YIN             (what we will port to C++)

Usage:
  python pitch_lab.py              # benchmark on synthetic strings + plot of a low E
  python pitch_lab.py recordings/E2.wav [more.wav ...]   # track pitch over your recordings
"""

import sys
import numpy as np
import matplotlib.pyplot as plt
from scipy.io import wavfile
from scipy.signal import butter, lfilter

FS    = 48000   # sample rate (Hz) - same as the Mac's audio device
FRAME = 2048    # samples per analysis frame (~43 ms) - the C++ app will use the same
FMIN, FMAX = 60.0, 1000.0   # search range: below low E (82 Hz), above high e 12th fret

NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
STRINGS = {"E2": 82.41, "A2": 110.00, "D3": 146.83, "G3": 196.00, "B3": 246.94, "E4": 329.63}


# ---------------------------------------------------------------- helpers
def freq_to_note(f):
    """Frequency -> (note name, cents off). n = 12*log2(f/440) semitones from A4."""
    n = 12.0 * np.log2(f / 440.0)
    nearest = int(round(n))
    cents = 100.0 * (n - nearest)
    midi = 69 + nearest
    return NOTE_NAMES[midi % 12] + str(midi // 12 - 1), cents


def parabolic(y, i):
    """Fit a parabola through y[i-1], y[i], y[i+1]; return the fractional index of its
    peak/valley. This gives sub-sample precision, which is what makes cents possible."""
    if i <= 0 or i >= len(y) - 1:
        return float(i)
    denom = y[i - 1] - 2.0 * y[i] + y[i + 1]
    if denom == 0:
        return float(i)
    offset = 0.5 * (y[i - 1] - y[i + 1]) / denom
    return i + float(np.clip(offset, -1.0, 1.0))   # never move more than one sample


# ---------------------------------------------------------------- test signal
def plucked_string(f0, seconds=1.0, fs=FS, seed=0):
    """Karplus-Strong plucked string (a classic physical model):
    a delay line of N samples filled with noise, fed back through a 2-point average.
    Returns the signal and its exact frequency fs/(N - 0.5)."""
    N = int(round(fs / f0 + 0.5))
    rng = np.random.default_rng(seed)
    buf = rng.uniform(-1.0, 1.0, N)
    buf -= np.roll(buf, int(0.2 * N))         # pluck at 1/5 of the string -> richer harmonics
    out = np.zeros(int(seconds * fs))
    for n in range(len(out)):
        i = n % N
        out[n] = buf[i]
        buf[i] = 0.996 * 0.5 * (buf[i] + buf[(i + 1) % N])
    return out / np.max(np.abs(out)), fs / (N - 0.5)


def laptop_mic(x, fs=FS):
    """A laptop/phone mic barely picks up low frequencies: model it as a 150 Hz high-pass."""
    b, a = butter(2, 150.0 / (fs / 2), btype="high")
    return lfilter(b, a, x)


# ---------------------------------------------------------------- A) FFT peak
def pitch_fft(x, fs):
    X = np.abs(np.fft.rfft(x * np.hanning(len(x))))
    freqs = np.fft.rfftfreq(len(x), 1.0 / fs)
    lo, hi = int(FMIN * len(x) / fs), int(FMAX * len(x) / fs)
    k = lo + int(np.argmax(X[lo:hi]))
    f = parabolic(X, k) * fs / len(x)          # interpolate between bins
    return f, freqs, X


# ---------------------------------------------------------------- B) autocorrelation
def pitch_autocorr(x, fs):
    """r[lag] = sum x[n] x[n+lag]. A periodic signal matches itself best when shifted by
    one period T, so the highest peak (after lag 0) is at lag = T."""
    x = x - np.mean(x)
    r = np.correlate(x, x, mode="full")[len(x) - 1:]
    r = r / r[0]
    lag_min, lag_max = int(fs / FMAX), int(fs / FMIN)
    lag = lag_min + int(np.argmax(r[lag_min:lag_max]))
    return fs / parabolic(r, lag), r


# ---------------------------------------------------------------- C) YIN
OCTAVE_RATIO = 0.4


def dip_bottom(y, i):
    """Value at the bottom of a parabola through y[i-1], y[i], y[i+1]. Comparing dips this way
    stops sample rounding from making the 2*tau dip look deeper on very clean tones."""
    if i <= 0 or i >= len(y) - 1:
        return y[i]
    a, b, c = y[i - 1], y[i], y[i + 1]
    den = a - 2 * b + c
    return b if den <= 0 else max(0.0, b - (a - c) ** 2 / (8 * den))


def pitch_yin(x, fs, threshold=0.15, octave_check=True):
    """YIN (de Cheveigne & Kawahara, 2002).
    1) difference function   d(tau) = sum (x[n] - x[n+tau])^2      -> 0 at the period
    2) normalise:            d'(tau) = d(tau) * tau / sum_{j<=tau} d(j)
    3) take the FIRST tau where d' < threshold (not the global minimum!) -> no octave errors
    3b) octave check: if d'(2*tau) is much smaller than d'(tau), the true period is 2*tau.
        Needed because a laptop/phone mic barely hears 82 Hz: after the attack the low E
        string's fundamental is ~30 dB below its 2nd harmonic ("missing fundamental").
    4) parabolic interpolation for sub-sample precision
    Returns (frequency, confidence 0..1, d')."""
    W = len(x) // 2
    tau_min, tau_max = int(fs / FMAX), int(fs / FMIN)

    d = np.zeros(tau_max + 1)
    for tau in range(1, tau_max + 1):                 # step 1
        diff = x[:W] - x[tau:tau + W]
        d[tau] = np.sum(diff * diff)

    dn = np.ones(tau_max + 1)                         # step 2
    running_sum = 0.0
    for tau in range(1, tau_max + 1):
        running_sum += d[tau]
        dn[tau] = d[tau] * tau / running_sum if running_sum > 0 else 1.0

    best = -1                                          # step 3
    for tau in range(tau_min, tau_max):
        if dn[tau] < threshold:
            while tau + 1 < tau_max and dn[tau + 1] < dn[tau]:
                tau += 1                               # slide down to the bottom of the dip
            best = tau
            break
    if best < 0:                                       # nothing clear: fall back to global min
        best = tau_min + int(np.argmin(dn[tau_min:tau_max]))

    while octave_check and 2 * best + 2 < tau_max:     # step 3b
        lo = 2 * best - 2
        double = lo + int(np.argmin(dn[lo:lo + 5]))     # exact dip near 2*tau
        if dip_bottom(dn, double) < OCTAVE_RATIO * dip_bottom(dn, best):   # interpolated, not raw samples
            best = double
        else:
            break

    tau_exact = parabolic(dn, best)                    # step 4
    return fs / tau_exact, 1.0 - dn[best], dn


# ---------------------------------------------------------------- experiments
def describe(label, f, true_f=None):
    name, cents = freq_to_note(f)
    line = f"  {label:<16} {f:8.2f} Hz   -> {name:<4} {cents:+6.1f} cents"
    if true_f:
        line += f"   (error {1200 * np.log2(f / true_f):+8.1f} cents)"
    print(line)


def benchmark():
    print(f"Synthetic strings, slightly out of tune, heard through a laptop mic")
    print(f"frame = {FRAME} samples = {1000 * FRAME / FS:.1f} ms, FFT bin width = {FS / FRAME:.1f} Hz\n")
    for name, f_nominal in STRINGS.items():
        x, f_true = plucked_string(f_nominal * 2 ** (-12 / 1200))     # about 12 cents flat
        x = laptop_mic(x)
        frame = x[int(0.2 * FS):int(0.2 * FS) + FRAME]                 # skip the pick attack
        print(f"{name}: true pitch {f_true:.2f} Hz")
        describe("A) FFT peak", pitch_fft(frame, FS)[0], f_true)
        describe("B) autocorr", pitch_autocorr(frame, FS)[0], f_true)
        describe("C) YIN", pitch_yin(frame, FS)[0], f_true)
        print()


def analyse(x, fs, title, true_f=None, png="pitch_lab.png"):
    start = int(0.2 * fs)
    frame = x[start:start + FRAME]

    f_fft, freqs, X = pitch_fft(frame, fs)
    f_ac, r = pitch_autocorr(frame, fs)
    f_yin, conf, dn = pitch_yin(frame, fs)

    print(title)
    describe("A) FFT peak", f_fft, true_f)
    describe("B) autocorr", f_ac, true_f)
    describe("C) YIN", f_yin, true_f)
    print(f"  YIN confidence: {conf:.2f}")

    fig, ax = plt.subplots(4, 1, figsize=(10, 11))
    t = np.arange(FRAME) / fs * 1000
    ax[0].plot(t, frame)
    ax[0].set(title=f"{title}: one {FRAME}-sample frame", xlabel="time (ms)")

    ax[1].plot(freqs, 20 * np.log10(X / X.max() + 1e-9))
    ax[1].axvline(f_fft, color="r", ls="--", label=f"FFT peak {f_fft:.1f} Hz")
    if true_f:
        ax[1].axvline(true_f, color="g", ls=":", label=f"true f0 {true_f:.1f} Hz")
    ax[1].set(xlim=(0, 1000), ylim=(-60, 3), title="A) spectrum", xlabel="Hz", ylabel="dB")
    ax[1].legend()

    lags = np.arange(len(r))
    ax[2].plot(lags[:int(fs / FMIN) + 50], r[:int(fs / FMIN) + 50])
    ax[2].axvline(fs / f_ac, color="r", ls="--", label=f"peak -> {f_ac:.2f} Hz")
    ax[2].set(title="B) autocorrelation", xlabel="lag (samples)")
    ax[2].legend()

    ax[3].plot(dn)
    ax[3].axhline(0.15, color="gray", ls=":", label="threshold")
    ax[3].axvline(fs / f_yin, color="r", ls="--", label=f"first dip -> {f_yin:.2f} Hz")
    ax[3].set(title="C) YIN normalised difference d'(tau)", xlabel="tau (samples)", ylim=(0, 1.5))
    ax[3].legend()

    fig.tight_layout()
    fig.savefig(png, dpi=110)
    print(f"\nPlot saved to {png}")


def track(x, fs, title, png, hop=1024, gate_db=-45.0):
    """Run all three detectors frame by frame over a whole recording - this is exactly
    what the app will do live. Frames quieter than gate_db are skipped (noise gate)."""
    times, level, f_fft, f_ac, f_yin, conf = [], [], [], [], [], []
    for start in range(0, len(x) - FRAME, hop):
        frame = x[start:start + FRAME]
        db = 20 * np.log10(np.sqrt(np.mean(frame ** 2)) + 1e-12)
        times.append((start + FRAME / 2) / fs)
        level.append(db)
        if db < gate_db:
            f_fft.append(np.nan); f_ac.append(np.nan); f_yin.append(np.nan); conf.append(0.0)
            continue
        f_fft.append(pitch_fft(frame, fs)[0])
        f_ac.append(pitch_autocorr(frame, fs)[0])
        f, c, _ = pitch_yin(frame, fs)
        f_yin.append(f); conf.append(c)
    times, level = np.array(times), np.array(level)
    f_fft, f_ac, f_yin, conf = map(np.array, (f_fft, f_ac, f_yin, conf))

    # split into plucks: each run of consecutive frames above the gate
    print(title)
    active = ~np.isnan(f_yin)
    edges = np.flatnonzero(np.diff(np.r_[0, active.astype(int), 0]))
    for k, (a, b) in enumerate(zip(edges[::2], edges[1::2])):
        a2 = min(a + 3, b - 1)                     # skip ~70 ms of pick attack
        good = slice(a2, b)
        if b - a2 < 3:
            continue
        def summary(f):
            name, _ = freq_to_note(np.nanmedian(f[good]))
            cents = [freq_to_note(v)[1] for v in f[good] if not np.isnan(v)]
            return f"{name:<4}{np.median(cents):+6.1f}c"
        print(f"  pluck {k + 1} @ {times[a]:5.1f}s  ({(b - a) * hop / fs:4.1f}s)"
              f"   FFT {summary(f_fft)}   autocorr {summary(f_ac)}   YIN {summary(f_yin)}"
              f"   (YIN spread ±{np.nanstd([freq_to_note(v)[1] for v in f_yin[good]]):.1f}c)")

    fig, ax = plt.subplots(2, 1, figsize=(11, 7), sharex=True)
    ax[0].plot(times, level)
    ax[0].axhline(gate_db, color="gray", ls=":", label="noise gate")
    ax[0].set(ylabel="level (dB)", title=f"{title}: level")
    ax[0].legend()
    ax[1].semilogy(times, f_fft, ".", ms=3, label="A) FFT peak", alpha=0.5)
    ax[1].semilogy(times, f_ac, ".", ms=3, label="B) autocorr")
    ax[1].semilogy(times, f_yin, ".", ms=3, label="C) YIN")
    for f in STRINGS.values():
        ax[1].axhline(f, color="gray", lw=0.5)
    ax[1].set(ylabel="pitch (Hz, log)", xlabel="time (s)", ylim=(60, 1100), title="detected pitch")
    ax[1].legend()
    fig.tight_layout()
    fig.savefig(png, dpi=100)
    plt.close(fig) if "--show" not in sys.argv else None
    print(f"  plot: {png}\n")


if __name__ == "__main__":
    files = [a for a in sys.argv[1:] if not a.startswith("--")]
    if files:
        for path in files:
            fs, x = wavfile.read(path)
            x = x.astype(np.float64)
            if x.ndim > 1:
                x = x.mean(axis=1)                 # stereo -> mono
            track(x, fs, path, png=path.rsplit(".", 1)[0] + "_track.png")
    else:
        benchmark()
        x, f_true = plucked_string(STRINGS["E2"] * 2 ** (-12 / 1200))
        analyse(laptop_mic(x), FS, "Synthetic low E (laptop mic)", f_true)
    if "--show" in sys.argv:
        plt.show()
