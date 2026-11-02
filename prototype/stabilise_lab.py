"""
Step 6 - turning raw YIN readings into a stable tuner display.

Runs YIN frame by frame (like the app), then applies the Step 6 chain:
  1) noise gate + onset hold-off   (ignore ~70 ms after a pluck)
  2) confidence check               (YIN confidence >= MIN_CONFIDENCE)
  3) median of the last 5 readings  (removes single-frame spikes)
  4) note lock                      (a ringing string can't jump a semitone: readings far
                                     from the current note are outliers, unless they persist)
  5) one-pole smoothing of cents    (same filter as the level meter)

Usage:  python stabilise_lab.py recordings/E2.wav [more.wav ...]
"""

import sys
import numpy as np
import matplotlib.pyplot as plt
from scipy.io import wavfile
import pitch_lab as L

HOP = 1024
GATE_DB = -62.0            # just above a quiet room; YIN's confidence rejects noise anyway
RELEASE_FRAMES = 16        # ~0.35 s: keep showing the note this long after it goes quiet...
MIN_NOTE_FRAMES = 8        # ...but only if it lasted ~0.2 s (a click must not stick on screen)
ONSET_JUMP_DB = 6.0        # level rises this much from one frame to the next -> new pluck
HOLD_FRAMES = 3            # ~70 ms ignored after a pluck
MIN_CONFIDENCE = 0.85
MEDIAN_SIZE = 5
START_FRAMES = 3           # valid readings needed before showing a note after a pluck
NOTE_CHANGE_FRAMES = 10    # ~0.2 s of consistent outliers -> accept a new note without a pluck
SMOOTH_TAU = 0.15          # seconds


def semitones(f):
    return 12.0 * np.log2(f / 440.0)          # 0 = A4, 1 per semitone


def run(x, fs):
    frame_time = HOP / fs
    a = np.exp(-frame_time / SMOOTH_TAU)

    history = []                 # last MEDIAN_SIZE valid readings, in semitones
    note = None                  # currently displayed note (integer semitone)
    candidate, candidate_count = None, 0
    cents_smooth = 0.0
    hold = 0
    prev_db = -100.0
    quiet = 0                    # frames in a row without a usable reading
    age = 0                      # usable readings since the current note was chosen
    fresh = False                # a new pluck started: the next agreeing readings choose the note

    times, raw, stable = [], [], []
    for start in range(0, len(x) - L.FRAME, HOP):
        frame = x[start:start + L.FRAME]
        db = 20 * np.log10(np.sqrt(np.mean(frame ** 2)) + 1e-12)
        times.append((start + L.FRAME / 2) / fs)

        shown = np.nan if note is None else note + cents_smooth / 100

        # 1) onset: a new pluck starts a fresh note, but keep showing the old one until
        #    the new readings agree (otherwise the display blinks off at every pluck)
        if db - prev_db > ONSET_JUMP_DB:
            hold = HOLD_FRAMES
            history, fresh = [], True
        prev_db = db

        #    gate + release: below the gate, keep the note for RELEASE_FRAMES, then clear it
        if db < GATE_DB:
            raw.append(np.nan)
            quiet += 1
            if quiet > (RELEASE_FRAMES if age >= MIN_NOTE_FRAMES else 0):
                note, history = None, []
            stable.append(np.nan if note is None else shown)
            continue

        f, conf, _ = L.pitch_yin(frame, fs)
        raw.append(semitones(f) if f > 0 else np.nan)

        if hold > 0:
            hold -= 1
            stable.append(shown)
            continue

        # 2) confidence (no clear pitch for a while counts as quiet too)
        if f <= 0 or conf < MIN_CONFIDENCE:
            quiet += 1
            if quiet > (RELEASE_FRAMES if age >= MIN_NOTE_FRAMES else 0):
                note, history = None, []
            stable.append(np.nan if note is None else shown)
            continue
        quiet = 0
        reading = semitones(f)

        # 4) note lock: outliers far from the current note don't enter the median
        if note is not None and not fresh and abs(reading - note) > 0.5:
            nearest = int(round(reading))
            candidate_count = candidate_count + 1 if nearest == candidate else 1
            candidate = nearest
            if candidate_count < NOTE_CHANGE_FRAMES:
                stable.append(shown)
                continue
            note, history, age = nearest, [], 0   # it really changed (e.g. big peg turn)
            cents_smooth = 100 * (reading - note)
        candidate, candidate_count = None, 0

        # 3) median filter
        history.append(reading)
        history = history[-MEDIAN_SIZE:]
        s = float(np.median(history))

        if note is None or fresh:                 # just after a pluck: wait until readings agree
            recent = history[-START_FRAMES:]
            if len(recent) < START_FRAMES or max(recent) - min(recent) > 0.5:
                stable.append(shown)              # meanwhile keep the old note (NaN if none)
                continue
            new_note = int(round(s))
            if new_note != note:
                cents_smooth = 100 * (s - new_note)
            if new_note != note:
                age = 0
            note, fresh = new_note, False

        age += 1

        # 5) smoothing of the cents
        target = np.clip(100 * (s - note), -50, 50)
        cents_smooth = a * cents_smooth + (1 - a) * target
        stable.append(note + cents_smooth / 100)

    return np.array(times), np.array(raw), np.array(stable)


def name(semi):
    midi = 69 + int(round(semi))
    return L.NOTE_NAMES[midi % 12] + str(midi // 12 - 1)


if __name__ == "__main__":
    for path in sys.argv[1:]:
        fs, x = wavfile.read(path)
        x = x.astype(np.float64)
        t, raw, stable = run(x, fs)

        ok = ~np.isnan(stable)
        nominal = int(round(np.nanmedian(stable)))
        wrong_raw = np.sum(np.round(raw[~np.isnan(raw)]) != nominal)
        wrong_stable = np.sum(np.round(stable[ok]) != nominal)
        def jitter(v):
            d = np.diff(v * 100)
            d = d[~np.isnan(d) & (np.abs(d) < 50)]      # same pluck, same note
            return np.std(d)
        jitter_raw, jitter_stable = jitter(raw), jitter(stable)
        print(f"{path}: note {name(nominal)} | wrong-note frames raw {wrong_raw} -> stable {wrong_stable}"
              f" | frame-to-frame jitter raw ±{jitter_raw:.1f}c -> stable ±{jitter_stable:.1f}c")

        fig, ax = plt.subplots(figsize=(11, 4))
        ax.plot(t, 100 * (raw - nominal), ".", ms=3, color="tab:orange", label="raw YIN (Step 5)")
        ax.plot(t, 100 * (stable - nominal), "-", lw=2, color="tab:green", label="stabilised (Step 6)")
        ax.axhspan(-3, 3, color="green", alpha=0.1, label="±3 cents = in tune")
        ax.set(ylim=(-60, 60), xlabel="time (s)", ylabel=f"cents from {name(nominal)}",
               title=f"{path}: what the needle would show")
        ax.legend(loc="lower right")
        fig.tight_layout()
        png = path.rsplit(".", 1)[0] + "_stable.png"
        fig.savefig(png, dpi=100)
        plt.close(fig)
