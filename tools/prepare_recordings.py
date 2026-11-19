"""Build the mono 24 kHz S16LE loops embedded in the field-recorded presets."""

import argparse
from pathlib import Path
import subprocess

import numpy as np
from scipy import ndimage, signal


RATE = 24000
# Start and duration in seconds in stationary portions of the supplied WAVs.
T72 = [(1000, 27, 4.6), (1200, 23.25, 4.5), (1400, 28.5, 4.7), (1600, 19.5, 4.5),
       (1800, 8.25, 4.6), (2200, 9.75, 4.8), (2400, 27, 4.6), (2600, 15, 4.7)]
# The MP3 has no tachometer. Each row gives playback RPM, engine start, air start
# (after the recorded spool-up), and the nominal loop duration, in seconds.
T90 = [(950, 0.75, 13.65, 4.35), (1450, 5.0, 14.8, 4.55), (1950, 15.05, 13.1, 4.65)]
# Game RPM, labelled T-72 donor, legacy take, observed low order (Hz),
# excerpt centres in 22-28-05, and target RMS before the mixer.
T90_HYBRID = [
    (680, 1000, 0, 101, (10.49, 11.68, 12.41, 13.72, 14.73), .092),
    (950, 1000, 0, 102, (10.73, 11.86, 12.72, 13.93, 15.06), .117),
    (1450, 1400, 1, 124, (10.99, 12.19, 13.33, 14.27, 15.42, 16.02), .153),
    (1950, 1800, 2, 121, (3.86, 4.36, 4.92, 5.57, 6.66, 7.28), .179),
    (2230, 2200, 2, 130, (1.25, 1.71, 2.14, 2.63, 8.91, 9.39), .195),
]
# Seconds in the two supplied MP4s, in the order listed in the calibration notes.
# Group matching parts of separate throttle blips to hold a stable game RPM.
LEOPARD1 = [
    (850, (8.2, 12.1, 38.9, 40.9, 54.2), (6.4, 13.1)),
    (1150, (33.72,), (21.6, 22.15, 22.4)),
    (1600, (22.9, 33.96, 34.02), (23.8, 24.05, 24.25)),
    (2050, (23.6, 24.15, 26.15, 35.12, 35.24), (24.85,)),
]
LEOPARD2 = [
    (840, (), (49.9, 52.3, 53.6, 105.8, 106.8, 126.0)),
    (1200, (31.97, 32.22), (110.2, 110.8, 111.15)),
    (1600, (33.04, 33.2, 33.33), (113.55, 113.8, 113.91)),
    (2300, (34.12, 34.26, 34.42), (114.8, 114.96)),
]
# Same frequency range during a real lift. Gains retain the drop in pressure
# at comparable pitch on each mic. The idle take is shared by both paths.
COAST = {
    "leopard1": {1150: (-2.5, (37.17, 37.33, 37.49), ()),
                 1600: (-4.0, (36.43, 36.58, 36.72), (25.54,)),
                 2050: (-4.5, (35.76, 35.89, 36.02), ())},
    "leopard2": {1200: (-4.5, (36.86, 36.99, 37.10), ()),
                 1600: (-7.0, (36.18, 36.30, 36.43), ()),
                 2300: (-7.0, (35.13, 35.24, 35.39), ())},
}
MARDER = [
    (900, (1.9, 22.6, 23.7, 24.5, 28.35)),
    (1450, (3.5, 4.35, 10.3, 11.75, 12.5, 21.5)),
    (2000, (8.7, 14.1, 14.8, 16.2, 17.8, 18.5)),
]
PUMA = [
    (1230, (4.0, 5.2, 8.0, 26.4, 50.0), (22.0, 32.0, 42.0), (), ()),
    (1650, (), (55.0,), (17.0, 20.0, 23.0, 26.0, 49.0), ()),
    (2250, (), (80.9, 81.25, 96.95, 97.4, 97.85), (), (58.7, 59.1)),
]
BRADLEY_FRONT = (1.15, 1.49, 1.74, 5.32, 5.66)
BRADLEY = [
    (800, (67.85, 68.25, 68.6, 68.95, 69.25), (), ()),
    (1360, (), BRADLEY_FRONT, ()),
    (1830, (), (2.06, 2.65, 3.48, 4.56, 5.03), (1.21, 1.54)),
    (2120, (), (), (2.81, 3.17, 3.72, 4.14, 4.93, 5.44)),
]
HUMMER_H2 = [
    (750, (6.91, 12.95, 13.85, 14.55, 14.83, 15.10)),
    (1350, (7.78, 7.98, 9.60, 16.63)),
    (1950, (4.0, 17.38, 17.60, 17.79)),
    (2850, (19.25, 19.40, 19.56)),
    (3900, (20.32, 20.58, 20.82, 21.08)),
]
# Quiet parts of the V6 microphone take. The prominent track squeals around
# 8–16 s, the edit near 39 s and the transient near 54 s are outside these grains.
BMP12 = [
    (1580, (1.40, 2.43, 3.08, 4.65, 5.12, 19.55)),
    (2120, (24.30, 24.60, 26.58, 44.52, 44.95, 55.39)),
    (2780, (28.70, 29.15, 29.60, 32.65, 33.45, 35.65, 48.80)),
]
BMP3 = [
    (1200, (2.52, 26.45, 35.53, 41.14, 46.45)),
    (1560, (20.70, 23.52, 24.45, 32.72, 33.24, 37.15)),
    (1980, (51.23, 51.65, 55.84, 57.12, 60.60, 62.65, 68.35)),
]
UAZ = [
    (620, (10.62, 11.46, 12.15, 12.86, 13.51, 14.32), (), ()),
    (1200, (), (3.18, 3.67, 4.14, 4.53, 5.20, 5.74, 6.24), ()),
    (1800, (), (), (10.92, 11.47, 12.28, 13.10, 14.05, 55.18, 55.94)),
    (2490, (), (), (35.52, 36.41, 37.35, 38.27, 39.13)),
    (2940, (), (), (1.19, 1.74, 2.24, 3.75, 4.31)),
]
STRYKER_IDLE = (4.12, 6.64, 9.38, 12.16, 16.45, 20.32, 25.46, 29.19)
STRYKER = [
    (750,  (STRYKER_IDLE, (), (), ()), .083),
    (1060, ((), (1.12, 1.56, 4.93, 5.39, 51.5), (), ()), .113),
    (1400, ((), (10.60, 12.64, 13.55), (), (1.80, 2.37, 3.17)), .148),
    (1800, ((), (), (2.97, 3.53, 4.07, 5.86), (7.43, 8.12, 9.76)), .180),
]
ARIETE_IDLE = (54.65, 57.12, 59.47, 124.80, 126.73, 129.44, 142.52, 145.32)
ARIETE = [
    (960, 96, (74.98, 75.45, 83.52, 91.86, 105.64, 112.66, 119.10), .145),
    (1160, 110, (63.26, 65.38, 76.14, 84.62, 106.28, 113.20, 119.95), .185),
]
M113 = [
    (650, 64, (1.35, 2.14, 2.82, 3.57, 4.28, 5.13), .083),
    (1010, 101, (7.86, 8.15, 8.52, 8.79, 18.23), .126),
    (1300, 130, (10.55, 10.93, 11.22, 11.51, 13.86, 14.46), .161),
    # Upper game layer: transpose the stationary high rev while keeping the
    # broadband colour of the actual microphone. The next two clips are driving.
    (1800, 130, (10.55, 10.93, 11.22, 11.51, 13.86, 14.46), .183),
]
# Pedal up/down transitions in the 2.3 dCi WAV. Reserve the last complete
# blip for comparison against the finished preset.
DCI_BLIPS = [(33.72, 34.73, 35.53), (35.53, 36.02, 36.98),
             (36.98, 37.45, 38.31), (38.31, 38.83, 39.76)]
# Trims are relative game-mix levels. Different video recorders do not share
# a sound-pressure reference. Orders are acoustic tuning guides in Hz/playback RPM.
VIDEO_TUNING = {
    "leopard1": (0.040, 0.095, 0.199), "leopard2": (0.050, 0.082, 0.204),
    "puma": (5 / 60, 0.104, 0.175), "bradley": (4 / 60, 0.077, 0.182),
    "hummer_h2": (4 / 60, 0.068, 0.205), "marder": (None, 0.128, 0.183),
    "uaz": (2 / 60, 0.079, 0.191),
}


def decode(path, ffmpeg):
    pcm = subprocess.check_output([ffmpeg, "-v", "error", "-i", str(path), "-ac", "1",
                                   "-ar", str(RATE), "-f", "f32le", "-"])
    x = np.frombuffer(pcm, dtype="<f4")
    # Remove subsonic microphone rumble before selecting a periodic seam.
    return signal.sosfiltfilt(signal.butter(2, 16, fs=RATE, btype="highpass", output="sos"), x)


def loop(x, start, seconds, steady_air=False):
    x = x[round(start * RATE):]
    length, search, fade = round(seconds * RATE), round(0.13 * RATE), round(0.12 * RATE)
    candidates = x[length-search:length+search+fade]
    head = x[:fade]
    cross = signal.correlate(candidates, head, mode="valid", method="fft")
    energy = np.convolve(candidates**2, np.ones(fade), mode="valid")
    end = length - search + np.argmax(cross / np.sqrt(energy * np.dot(head, head) + 1e-20))
    out = x[:end].copy()
    weight = 0.5 - 0.5 * np.cos(np.linspace(0, np.pi, fade))
    out[:fade] = x[end:end+fade] * (1 - weight) + head * weight
    if steady_air:
        # The bass is phase-matched. The two short stretches of air are uncorrelated.
        # Use equal power for the latter so the join does not make the whistle dip.
        air = signal.sosfiltfilt(signal.butter(3, 1400, fs=RATE, btype="highpass", output="sos"), x)
        out[:fade] += (np.sqrt(1 - weight) - (1 - weight)) * air[end:end+fade]
        out[:fade] += (np.sqrt(weight) - weight) * air[:fade]
    return out


def steady_rotation(x):
    # Follow the strongest low engine order through the take. Correct the slow
    # speed drift in source time while preserving the timing of individual pulses.
    freq, times, spectrum = signal.stft(x, RATE, nperseg=4096, noverlap=3856,
                                        nfft=8192, boundary=None, padded=False)
    bins = np.flatnonzero((freq >= 94) & (freq <= 165))
    peak = bins[np.argmax(np.abs(spectrum[bins]), axis=0)]
    frames = np.arange(len(times))
    logmag = np.log(np.maximum(np.abs(spectrum), 1e-12))
    left, middle, right = (logmag[peak + k, frames] for k in (-1, 0, 1))
    curvature = np.minimum(left - 2 * middle + right, -1e-9)
    offset = np.clip(0.5 * (left - right) / curvature, -0.5, 0.5)
    order = (peak + offset) * (freq[1] - freq[0])
    order = ndimage.gaussian_filter1d(ndimage.median_filter(order, 9), 6)
    sample = np.arange(len(x))
    clock = np.cumsum(np.interp(sample / RATE, times, order))
    clock -= clock[0]
    clock *= sample[-1] / clock[-1]
    return np.interp(sample, clock, x)


def steady_gain(spectrum, freq):
    # Average over neighbouring bins and about 0.2 s. Individual engine pulses
    # retain their dynamics as the slower changes in the moving take are reduced.
    slow_power = ndimage.gaussian_filter(np.abs(spectrum)**2, sigma=(2.0, 8.0))
    slow_db = 10 * np.log10(np.maximum(slow_power, 1e-20))
    amount = np.interp(freq, [125, 950, 2000], [0.63, 0.82, 0.98])
    correction = (np.median(slow_db, axis=1, keepdims=True) - slow_db) * amount[:, None]
    return 10**(np.clip(correction, -15, 12) / 20)


def t90_loop(source, body_start, air_start, seconds):
    # Work with margins for the spectral windows and the seam search, then trim.
    margin = 0.25
    count = round((seconds + 2 * margin + 0.30) * RATE)

    def excerpt(start):
        first = round((start - margin) * RATE)
        take = source[first:first + count]
        if len(take) != count:
            raise ValueError("T-90M recording is too short for the selected steady intervals")
        return take

    window = dict(fs=RATE, nperseg=2048, noverlap=1808)
    freq, _, body = signal.stft(steady_rotation(excerpt(body_start)), **window)
    _, _, air = signal.stft(excerpt(air_start), **window)
    # Remove isolated whistles from the upper microphone band. The broadband air
    # and its short transients keep their recorded phase and fine texture.
    air_power = np.abs(air)**2
    floor = ndimage.median_filter(air_power, size=(31, 1))
    air *= np.minimum(1.0, np.sqrt(7.0 * floor / np.maximum(air_power, 1e-20)))
    blend = np.clip((freq - 950) / (1700 - 950), 0, 1)[:, None] * (np.pi / 2)
    spectrum = np.cos(blend) * body + np.sin(blend) * air

    # The group pass is unusually bass-heavy. Restore some exterior presence
    # using the balance of the stationary side view in the reference videos.
    presence = np.interp(np.log2(np.maximum(freq, 180)), np.log2([180, 600, 2000, 6000, 11000]),
                         [0, 5, 8, 8, 5])
    gain = steady_gain(spectrum, freq) * 10**(presence[:, None] / 20)
    _, steady = signal.istft(spectrum * gain, **window)
    return loop(steady, margin, seconds, steady_air=True)


def align_rotation(source, center, half, target_hz, source_hz=None, harmonics=((1, 1.0), (2, 0.65), (3, 0.3))):
    # Follow a low order locally through the short sweep, then read the take
    # on a uniform crank-angle grid. Both sides of an RPM crossfade agree in pitch.
    reach = round(0.37 * RATE)
    excerpt = source[center-reach:center+reach]
    freq, times, spectrum = signal.stft(excerpt, RATE, nperseg=3072, noverlap=2712,
                                        nfft=8192, boundary=None, padded=False)
    search_hz = target_hz if source_hz is None else source_hz
    candidates = np.linspace(0.77, 1.25, 97) * search_hz
    log_power = np.log(np.maximum(np.abs(spectrum), 1e-12))
    scores = np.zeros((len(candidates), len(times)))
    for harmonic, weight in harmonics:
        bins = candidates * harmonic / freq[1]
        index = bins.astype(int)
        scores += weight * (log_power[index] + (bins-index)[:, None] *
                            (log_power[index + 1] - log_power[index]))
    scores -= 2.0 * np.abs(np.log(candidates / search_hz))[:, None]
    hz = ndimage.gaussian_filter1d(ndimage.median_filter(candidates[scores.argmax(axis=0)], 3), 1.2)
    samples = np.arange(len(excerpt))
    angle = np.cumsum(np.interp(samples / RATE, times, hz / target_hz))
    return np.interp(np.arange(-half, half) + angle[reach], angle, excerpt)


def video_loop(sources, cues, rpm, seed, level, order_hz=None, source_hz=None, *, smooth_air=False,
               harmonics=((1, 1.0), (2, 0.65), (3, 0.3))):
    rng = np.random.default_rng(seed + rpm)
    takes, pulses = [], []
    for source, marks in zip(sources, cues, strict=True):
        for mark in marks:
            center = round(mark * RATE)
            half = round(0.21 * RATE)
            take = source[center-half:center+half]
            if len(take) != 2 * half or np.mean(take**2) < 1e-8:
                raise ValueError(f"Missing engine audio around {mark} seconds")
            scale = np.sqrt(np.mean(take**2))
            takes.append(take / scale)
            pulses.append(align_rotation(source, center, half, order_hz, source_hz, harmonics) / scale if order_hz else takes[-1])

    # Preserve the recorded pressure pulses at the bottom. Match the phase of
    # consecutive short grains so a complete acceleration never repeats.
    lowpass = signal.butter(4, 900, fs=RATE, output="sos")
    bodies = [signal.sosfiltfilt(lowpass, take) for take in pulses]
    overlap = round(0.060 * RATE)
    step = round(0.162 * RATE)
    weight = 0.5 - 0.5 * np.cos(np.linspace(0, np.pi, overlap))
    body = np.zeros(6 * RATE)
    order = rng.permutation(len(bodies))
    for n, pos in enumerate(range(0, len(body) - step - overlap, step)):
        if n and n % len(order) == 0:
            order = rng.permutation(len(bodies))
        grain = bodies[order[n % len(order)]]
        first = int(rng.integers(round(0.025 * RATE), round(0.080 * RATE)))
        candidates = grain[first:first + round(0.075 * RATE) + overlap]
        if pos:
            head = body[pos:pos + overlap]
            score = signal.correlate(candidates, head, mode="valid", method="fft")
            energy = np.convolve(candidates**2, np.ones(overlap), mode="valid")
            first += int(np.argmax(score / np.sqrt(np.maximum(energy, 1e-15))))
        segment = grain[first:first + step + overlap]
        body[pos:pos + overlap] *= 1 - weight
        body[pos:pos + overlap] += weight * segment[:overlap]
        body[pos + overlap:pos + step + overlap] = segment[overlap:]

    # Take the upper-band colour from the microphone spectra. A fresh noise
    # phase removes intelligible crowd/room events from the game loop.
    freq, densities = signal.welch(np.array(takes), RATE, nperseg=1024)
    density = np.median(densities, axis=0)
    if smooth_air:
        # Preserve the broad microphone colour. A lower-percentile envelope
        # removes thin metal squeals even if several passes share the same tone.
        floor = ndimage.percentile_filter(np.percentile(densities, 35, axis=0), 35, size=19)
        density = ndimage.gaussian_filter1d(floor, 3)
    noise = rng.standard_normal(len(body))
    fft_freq = np.fft.rfftfreq(len(body), 1 / RATE)
    colour = np.sqrt(np.interp(fft_freq, freq, density) * RATE / 2)
    air = np.fft.irfft(np.fft.rfft(noise) * colour, n=len(body))
    if smooth_air:
        pressure = np.sqrt(ndimage.gaussian_filter1d(body**2, 0.002 * RATE))
        envelope = 0.70 + 0.30 * np.minimum(2.0, pressure / np.mean(pressure))
        mixed = body + envelope * (air - signal.sosfiltfilt(lowpass, air))
    else:
        mixed = body + air - signal.sosfiltfilt(lowpass, air)
    window = dict(fs=RATE, nperseg=2048, noverlap=1808)
    freq, _, spectrum = signal.stft(mixed, **window)
    _, steady = signal.istft(spectrum * steady_gain(spectrum, freq), **window)
    result = loop(steady, 0.24, 5.05, steady_air=True)
    return result * (level / np.sqrt(np.mean(result**2)))


def bradley_loop(sources, cues, rpm, seed, level, firing_hz):
    # The hill take near 1360 is dominated by gear mesh during a shift. Use
    # the steady front pass, following its actual 121 Hz pulses before retuning.
    source_hz = 121 if rpm == 1360 else firing_hz
    data = video_loop(sources, cues, rpm, seed, level, firing_hz, source_hz)
    spectrum = np.fft.rfft(data)
    orders = np.fft.rfftfreq(len(data), 1 / RATE) / firing_hz
    pulse_band = (orders > 0.89) & (orders < 1.11)
    if rpm == 800:
        # At idle the uphill mic is distant. Keep its actual low firing pulses
        # and use the front mic for detail above them. Match both at the pulse,
        # then blend their circular spectra without introducing another seam.
        front = video_loop(sources, ((), BRADLEY_FRONT, ()), rpm, seed + 1, level, firing_hz, 121)
        detail = np.fft.rfft(signal.resample(front, len(data)))
        detail *= np.linalg.norm(spectrum[pulse_band]) / np.linalg.norm(detail[pulse_band])
        blend = 0.5 - 0.5 * np.cos(np.pi * np.clip((orders - 3.4) / 1.6, 0, 1))
        spectrum = (1 - blend) * spectrum + blend * detail
    energy = np.abs(spectrum)**2
    firing_energy = energy[pulse_band].sum()
    # The whine tracks about 5.28 times the firing frequency, with an octave
    # above it. Cap both narrow bands relative to the pulses on the circular
    # loop, so applying the correction cannot introduce a join or a level swell.
    for harmonic, ceiling_db in ((1, -28), (2, -32)):
        offset = np.abs(orders / harmonic - 5.28)
        band_energy = energy[offset <= 0.18].sum()
        excess_db = max(0, 10 * np.log10(max(band_energy, 1e-20) / firing_energy) - ceiling_db)
        edge = np.clip((offset - 0.18) / 0.08, 0, 1)
        weight = 0.5 + 0.5 * np.cos(np.pi * edge)
        spectrum *= 10**(-excess_db * weight / 20)
    clean = np.fft.irfft(spectrum, n=len(data))
    return clean * (level / np.sqrt(np.mean(clean**2)))


def write_pcm(path, data):
    np.rint(np.clip(data, -1, 1) * 32767).astype("<i2").tofile(path)


def prepare_t90m(mp3, video, recordings):
    output = recordings / "t90m"
    output.mkdir(exist_ok=True)
    legacy = [t90_loop(mp3, body, air, duration) for _, body, air, duration in T90]

    def match_length(take, count):
        # Keep the microphone pitch when matching the length of the donor loop.
        fade = round(.12 * RATE)
        extended = np.resize(take, count + fade)
        weight = .5 - .5 * np.cos(np.linspace(0, np.pi, fade))
        extended[:fade] = extended[count:] * (1 - weight) + extended[:fade] * weight
        return extended[:count] / np.sqrt(np.mean(take**2))

    for rpm, donor_rpm, old_take, order_hz, marks, level in T90_HYBRID:
        donor = np.fromfile(recordings / "t72" / f"{donor_rpm}.pcm", "<i2").astype(float)
        body = signal.resample(donor, round(len(donor) * donor_rpm / rpm))
        body /= np.sqrt(np.mean(body**2))
        exterior = match_length(legacy[old_take], len(body))
        if video is not None:
            # Take the midrange pressure from the new mic. Random-phase upper
            # texture keeps the recorded pass-by sweeps out of the sustained loop.
            take = video_loop([video], [marks], rpm, 9202, 1., rpm / 10, order_hz, smooth_air=True)
            exterior = .84 * match_length(take, len(body)) + .16 * exterior
        freq = np.fft.rfftfreq(len(body), 1 / RATE)
        blend = np.interp(freq, [0, 105, 320, 800, 2200], [.09, .09, .28, .70, .77]) * (np.pi / 2)
        spectrum = np.cos(blend) * np.fft.rfft(body) + np.sin(blend) * np.fft.rfft(exterior)
        mixed = np.fft.irfft(spectrum, n=len(body))
        write_pcm(output / f"{rpm}.pcm", mixed * level / np.sqrt(np.mean(mixed**2)))


def write_clean_pcm(path, data):
    # These are steady-RPM loops. Reduce slow microphone swells and excessive
    # broadband hiss while retaining individual pulses and narrow engine orders.
    spectrum = np.fft.rfft(data)
    hz = np.fft.rfftfreq(len(data), 1 / RATE)
    freq, density = signal.welch(data, RATE, nperseg=1536)
    noise_floor = ndimage.median_filter(density, size=17)
    broadband = np.clip(noise_floor / np.maximum(density, 1e-18), .05, 1.)
    crossover = .5 - .5 * np.cos(np.pi * np.clip((hz - 2600) / 2000, 0, 1))
    air = np.fft.irfft(spectrum * crossover * np.interp(hz, freq, broadband), n=len(data))
    body = np.fft.irfft(spectrum * ((hz >= 55) & (hz < 900)), n=len(data))

    def slow_level(component):
        power = ndimage.uniform_filter1d(component**2, round(.24 * RATE), mode="wrap")
        return 10 * np.log10(np.maximum(power, 1e-16))

    hi = density[(freq >= 4000) & (freq < 10500)].sum()
    mid = density[(freq >= 60) & (freq < 1500)].sum()
    trim = np.clip(.62 * (10 * np.log10(max(hi, 1e-16) / max(mid, 1e-16)) + 18.5), 0, 3.2)
    air_db = slow_level(air)
    if np.ptp(np.percentile(air_db, [10, 90])) > 3.0:
        trim = trim + np.clip(.80 * (air_db - np.percentile(air_db, 35) - .7), 0, 8.)
    cleaned = data + air * np.expm1(-trim * np.log(10) / 20)
    body_db = slow_level(body)
    if np.ptp(np.percentile(body_db, [10, 90])) > 3.0:
        correction = np.clip(.53 * (np.median(body_db) - body_db), -1.8, 1.8)
        cleaned *= 10**(correction / 20)
    write_pcm(path, cleaned)


def broadband_colour(source, cues):
    takes = np.array([source[round((t - .25) * RATE):round((t + .25) * RATE)] for t in cues])
    takes /= np.sqrt(np.mean(takes**2))
    freq, densities = signal.welch(takes, RATE, nperseg=1024)
    floor = ndimage.percentile_filter(np.percentile(densities, 35, axis=0), 40, size=13)
    return freq, np.sqrt(ndimage.gaussian_filter1d(floor, 2) * RATE / 2)


def isolate_engine(take, firing, order_step, crossover, freq, colour, air_gain, seed, level):
    # Restrict the original pressure signal to its repeated components below
    # the crossover. Rebuild the broadband texture with new phase so speech
    # formants and gear harmonics cannot recur as part of an engine cycle.
    bins = np.fft.rfftfreq(len(take), 1 / RATE)
    low, high = crossover
    mix = 0.5 - 0.5 * np.cos(np.pi * np.clip((bins - low) / (high - low), 0, 1))
    order = bins / firing
    offset = order - np.round(order / order_step) * order_step
    keep = 0.025 + 0.975 * np.exp(-0.5 * (offset / (0.15 * order_step))**2)
    pressure = np.fft.irfft(np.fft.rfft(take) * (1 - mix) * keep, n=len(take))
    pressure /= np.sqrt(np.mean(pressure**2))
    noise = np.random.default_rng(seed).standard_normal(len(take))
    air = np.fft.irfft(np.fft.rfft(noise) * np.interp(bins, freq, colour) * mix, n=len(take))
    envelope = np.sqrt(ndimage.gaussian_filter1d(pressure**2, .0025 * RATE, mode="wrap"))
    texture = pressure + air * (0.7 + .35 * np.minimum(envelope, 2)) * air_gain
    return texture * (level / np.sqrt(np.mean(texture**2)))


def prepare_stryker(sources, destination):
    # The moving clips are useful below the gearbox tones. The stationary
    # microphone supplies the entire upper-band texture at every speed.
    freq, colour = broadband_colour(sources[0], STRYKER_IDLE)
    for rpm, cues, level in STRYKER:
        firing = rpm / 20
        take = video_loop(sources, cues, rpm, 701, 1.0, firing, smooth_air=True)
        air_gain = np.interp(rpm, [750, 1800], [1., 1.55])
        data = isolate_engine(take, firing, .5, (215, 330), freq, colour, air_gain, 3126 + rpm, level)
        write_clean_pcm(destination / f"{rpm}.pcm", data)


def prepare_ariete(source, destination):
    freq, idle_colour = broadband_colour(source, ARIETE_IDLE)
    idle = loop(source, 124.40, 6.25, steady_air=True)
    write_clean_pcm(destination / "860.pcm", isolate_engine(idle, 86, .25, (440, 650), freq, idle_colour, 1., 1240, .100))
    # Use the stack of overtones in the rear microphone to follow the blips.
    # A single strong line also occurs in the overlapping bystanders' voices.
    harmonics = ((1, 1.2), (1.25, .9), (1.5, .85), (2, .6), (2.25, .6), (2.5, .4))
    for rpm, source_hz, cues, level in ARIETE:
        firing = rpm / 10
        take = video_loop([source], [cues], rpm, 121, 1., firing, source_hz, smooth_air=True, harmonics=harmonics)
        _, gas_colour = broadband_colour(source, cues)
        colour = .4 * idle_colour + .6 * gas_colour
        data = isolate_engine(take, firing, .25, (440, 650), freq, colour, 1.18, 1212 + rpm, level)
        write_clean_pcm(destination / f"{rpm}.pcm", data)


def dci_pulse_rate(revs):
    # Track the firing harmonics through the short blips in this recording.
    # A dip between neighbouring harmonics distinguishes the firing rate from
    # a strong octave. Continuity resolves the brief gaps at pedal changes.
    freq, times, spectrum = signal.stft(revs, RATE, nperseg=2304, noverlap=2064,
                                        nfft=8192, boundary=None, padded=False)
    logmag = 20 * np.log10(np.maximum(np.abs(spectrum), 1e-9))
    hz = np.arange(24., 107., 0.4)

    def amplitude(order):
        bins = hz * order / freq[1]
        index = bins.astype(int)
        return logmag[index] + (bins - index)[:, None] * (logmag[index + 1] - logmag[index])

    score = np.zeros((len(hz), len(times)))
    for h, weight in enumerate((1.3, 1.0, .9, .65, .5, .55, .35, .25), start=1):
        score += weight * (amplitude(h) - 0.45 * amplitude(h + .45) - 0.55 * amplitude(h - .45))
    delta = np.abs(np.log2(hz[:, None] / hz[None, :]))
    penalty = 140 * delta + 1600 * delta**2
    previous = np.zeros(score.shape, dtype=int)
    best = score[:, 0]
    for i in range(1, len(times)):
        paths = best[None, :] - penalty
        previous[:, i] = paths.argmax(axis=1)
        best = score[:, i] + paths.max(axis=1)
    track = np.empty(len(times), dtype=int)
    track[-1] = best.argmax()
    for i in range(len(times) - 1, 0, -1):
        track[i - 1] = previous[track[i], i]
    return times, hz[track]


def dci_loop(revs, times, hz, angle, intervals, rpm, seed):
    target = rpm / 30  # Four-cylinder four-stroke.
    period = RATE / target
    # Each grain contains twelve firing intervals, enough for three different
    # four-cylinder sequences. Read at constant crank speed to preserve the
    # relationship between the bass thud and the broadband injector clatter.
    phase = np.arange(round(12 * period)) / period - 6
    takes = []
    for start, end in intervals:
        first, last = round((start - 33) * RATE), round((end - 33) * RATE)
        frames = np.flatnonzero((times >= start - 33) & (times <= end - 33))
        when = times[frames[np.argmin(np.abs(hz[frames] - target))]]
        center = np.clip(angle[round(when * RATE)], angle[first] + 6, angle[last] - 6)
        takes.append(np.interp(center + phase, angle[first:last + 1], revs[first:last + 1]))

    # Match a whole cylinder sequence at each splice. The time-domain grains
    # retain their original high-frequency attacks through the crossfade.
    hop, overlap, search = (round(p * period) for p in (4, 2, 4))
    fade = 0.5 - 0.5 * np.cos(np.linspace(0, np.pi, overlap))
    rng = np.random.default_rng(seed + rpm)
    output = np.zeros(6 * RATE)
    order = rng.permutation(len(takes))
    for n, pos in enumerate(range(0, len(output) - hop - overlap, hop)):
        if n and n % len(takes) == 0:
            order = rng.permutation(len(takes))
        grain = takes[order[n % len(takes)]]
        first = int(rng.integers(0, round(period)))
        if pos:
            head = output[pos:pos + overlap]
            choices = grain[first:first + search + overlap]
            match = signal.correlate(choices, head, mode="valid", method="fft")
            energy = signal.convolve(choices**2, np.ones(overlap), mode="valid", method="fft")
            first += int(np.argmax(match / np.sqrt(np.maximum(energy, 1e-15))))
        grain = grain[first:first + hop + overlap]
        output[pos:pos + overlap] = output[pos:pos + overlap] * (1 - fade) + grain[:overlap] * fade
        output[pos + overlap:pos + hop + overlap] = grain[overlap:]
    return loop(output, 0.25, 5.10, steady_air=True)


def prepare_dci(source, destination):
    if len(source) < 42 * RATE:
        raise ValueError("Expected the supplied 44-second 2.3 dCi recording")
    idle = loop(source, 7.40, 7.20, steady_air=True)
    write_clean_pcm(destination / "825.pcm", idle * (0.065 / np.sqrt(np.mean(idle**2))))
    revs = source[33 * RATE:40 * RATE]
    times, hz = dci_pulse_rate(revs)
    angle = np.cumsum(np.interp(np.arange(len(revs)) / RATE, times, hz)) / RATE
    power = [(start, peak) for start, peak, end in DCI_BLIPS]
    lift = [(peak, end) for start, peak, end in DCI_BLIPS]
    # The microphone gets about 1.6 dB quieter on release at the middle
    # anchor. At the top, power and lift have nearly the same level.
    for rpm, on, off in ((1710, .096, .094), (2220, .143, .119), (2760, .201, .196)):
        for suffix, spans, level, seed in (("", power, on, 2300), ("-coast", lift, off, 2400)):
            data = dci_loop(revs, times, hz, angle, spans, rpm, seed)
            write_clean_pcm(destination / f"{rpm}{suffix}.pcm", data * (level / np.sqrt(np.mean(data**2))))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("wav_directory", type=Path, nargs="?")
    parser.add_argument("t90_mp3", type=Path, nargs="?")
    parser.add_argument("--ffmpeg", default="ffmpeg")
    parser.add_argument("--t90m", nargs=2, type=Path, metavar=("VIDEO", "MP3"),
                        help="build the T-90M hybrid from both references and the embedded T-72 donors")
    parser.add_argument("--leopard1", nargs=2, type=Path, metavar=("OUTDOORS", "HALL"))
    parser.add_argument("--leopard2", nargs=2, type=Path, metavar=("RAMP", "SQUARE"))
    parser.add_argument("--marder", type=Path, metavar="SQUARE")
    parser.add_argument("--puma", nargs=4, type=Path, metavar=("DISPLAY", "CLOSE", "TURN", "PASSES"))
    parser.add_argument("--bradley", nargs=3, type=Path, metavar=("HILL", "FRONT", "SIDE"))
    parser.add_argument("--hummer-h2", type=Path, metavar="BLACK_SUV")
    parser.add_argument("--bmp12", type=Path, metavar="WAV")
    parser.add_argument("--bmp3", type=Path, metavar="WAV")
    parser.add_argument("--dci23", type=Path, metavar="WAV")
    parser.add_argument("--uaz", nargs=3, type=Path, metavar=("WALKAROUND", "BONNET", "DRIVE"))
    parser.add_argument("--stryker", nargs=4, type=Path, metavar=("IDLE", "MANOEUVRE", "PASS", "APPROACH"))
    parser.add_argument("--ariete", type=Path, metavar="REAR")
    parser.add_argument("--m113", type=Path, metavar="STATIONARY")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1] / "assets" / "recordings"
    if bool(args.wav_directory) != bool(args.t90_mp3):
        parser.error("supply both legacy positional inputs, or a named recording")
    if not any(value for key, value in vars(args).items() if key != "ffmpeg"):
        parser.error("supply at least one recording")
    if args.wav_directory:
        (root / "t72").mkdir(parents=True, exist_ok=True)
        for rpm, start, duration in T72:
            path = args.wav_directory / f"T-72 tank engine operation at {rpm} rpm.wav"
            write_pcm(root / "t72" / f"{rpm}.pcm", loop(decode(path, args.ffmpeg), start, duration))
    if args.t90m or args.t90_mp3:
        video = decode(args.t90m[0], args.ffmpeg) if args.t90m else None
        mp3 = decode(args.t90m[1] if args.t90m else args.t90_mp3, args.ffmpeg)
        prepare_t90m(mp3, video, root)
    if args.dci23:
        (root / "dci23").mkdir(parents=True, exist_ok=True)
        prepare_dci(decode(args.dci23, args.ffmpeg), root / "dci23")
    if args.stryker:
        (root / "stryker").mkdir(parents=True, exist_ok=True)
        prepare_stryker([decode(path, args.ffmpeg) for path in args.stryker], root / "stryker")
    if args.ariete:
        (root / "ariete").mkdir(parents=True, exist_ok=True)
        prepare_ariete(decode(args.ariete, args.ffmpeg), root / "ariete")
    if args.m113:
        (root / "m113").mkdir(parents=True, exist_ok=True)
        source = decode(args.m113, args.ffmpeg)
        for rpm, source_hz, cues, level in M113:
            data = video_loop([source], [cues], rpm, 653, level, rpm / 10, source_hz, smooth_air=True)
            write_clean_pcm(root / "m113" / f"{rpm}.pcm", data)
    for name, path, anchors, pulses, levels, seed in (
        ("bmp12", args.bmp12, BMP12, 3, (.103, .145, .177), 2012),
        ("bmp3", args.bmp3, BMP3, 5, (.105, .148, .188), 2903),
    ):
        if path:
            (root / name).mkdir(parents=True, exist_ok=True)
            source = decode(path, args.ffmpeg)
            for (rpm, cues), level in zip(anchors, levels, strict=True):
                data = video_loop([source], [cues], rpm, seed, level, rpm * pulses / 60, smooth_air=True)
                write_clean_pcm(root / name / f"{rpm}.pcm", data)
    for name, videos, anchors, seed in [("leopard1", args.leopard1, LEOPARD1, 838),
                                        ("leopard2", args.leopard2, LEOPARD2, 873),
                                        ("marder", [args.marder] if args.marder else [], MARDER, 833),
                                        ("puma", args.puma, PUMA, 890), ("bradley", args.bradley, BRADLEY, 903),
                                        ("hummer_h2", [args.hummer_h2] if args.hummer_h2 else [], HUMMER_H2, 6000),
                                        ("uaz", args.uaz, UAZ, 469)]:
        if videos:
            (root / name).mkdir(parents=True, exist_ok=True)
            sources = [decode(path, args.ffmpeg) for path in videos]
            order, idle_level, full_level = VIDEO_TUNING[name]
            for rpm, *cues in anchors:
                level = np.interp(rpm, [anchors[0][0], anchors[-1][0]], [idle_level, full_level])
                order_hz = rpm * order if order else None
                # These stationary engines each have a second strong
                # idle resonance that a single-order speed correction would bend.
                if rpm == anchors[0][0] and name in ("leopard1", "hummer_h2", "uaz"):
                    order_hz = None
                if name == "bradley":
                    data = bradley_loop(sources, cues, rpm, seed, level, order_hz)
                else:
                    data = video_loop(sources, cues, rpm, seed, level, order_hz, smooth_air=name == "uaz")
                writer = write_clean_pcm if name == "uaz" else write_pcm
                writer(root / name / f"{rpm}.pcm", data)
                if rpm in COAST.get(name, {}):
                    trim_db, *lift = COAST[name][rpm]
                    data = video_loop(sources, lift, rpm, seed + 1709, level * 10**(trim_db / 20), order_hz)
                    write_pcm(root / name / f"{rpm}-coast.pcm", data)


if __name__ == "__main__":
    main()
