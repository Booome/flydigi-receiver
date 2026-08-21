#!/usr/bin/env python3
# Regenerate the notification alarm tone (notify_alarm.wav).
# Triangle wave (smooth edges, no square-wave clipping) with envelope.
import wave, struct, math, os

SR = 44100


def beep(freq, dur, vol=0.70):
    n = int(SR * dur)
    fade = int(SR * 0.010)  # 10ms fade in/out to avoid clicks
    out = []
    # triangle: ramps -1 -> +1 -> -1 over one period
    for i in range(n):
        phase = (i * freq / SR) % 1.0
        if phase < 0.5:
            s = -1.0 + 4.0 * phase
        else:
            s = 3.0 - 4.0 * phase
        if i < fade:
            env = i / fade
        elif i > n - fade:
            env = (n - i) / fade
        else:
            env = 1.0
        out.append(int(vol * 32767 * s * env))
    return out


def main():
    samples = []
    for _ in range(3):
        samples += beep(1000, 0.25)
        samples += [0] * int(SR * 0.10)
    samples += beep(1500, 0.4)

    out = os.path.join(os.path.dirname(__file__), "notify_alarm.wav")
    w = wave.open(out, "w")
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(SR)
    w.writeframes(struct.pack("<%dh" % len(samples), *samples))
    w.close()
    print("wrote %s (%d samples, triangle vol=0.70)" % (out, len(samples)))


if __name__ == "__main__":
    main()
