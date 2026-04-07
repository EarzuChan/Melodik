#!/usr/bin/env python3
import csv
import json
import math
import os
import sys
from typing import List, Tuple

from PIL import Image, ImageDraw


def load_waveform_csv(path: str) -> List[Tuple[float, float, float]]:
    rows = []
    with open(path, "r", encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f)
        for r in reader:
            rows.append((float(r["time_s"]), float(r["min_amp"]), float(r["max_amp"])))
    return rows


def load_f0_csv(path: str) -> List[Tuple[float, float, int]]:
    rows = []
    with open(path, "r", encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f)
        for r in reader:
            rows.append((float(r["time_s"]), float(r["hz"]), int(r["voiced"])))
    return rows


def load_segments_csv(path: str) -> List[Tuple[float, float]]:
    rows = []
    with open(path, "r", encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f)
        for r in reader:
            rows.append((float(r["start_s"]), float(r["end_s"])))
    return rows


def load_duration(meta_path: str, wave_rows, f0_rows, seg_rows) -> float:
    duration = 0.0
    if os.path.exists(meta_path):
        with open(meta_path, "r", encoding="utf-8") as f:
            meta = json.load(f)
            duration = max(duration, float(meta.get("duration_seconds", 0.0)))
    if wave_rows:
        duration = max(duration, wave_rows[-1][0])
    if f0_rows:
        duration = max(duration, f0_rows[-1][0])
    for s, e in seg_rows:
        duration = max(duration, s, e)
    return max(duration, 1.0)


def main():
    if len(sys.argv) < 3:
        print("Usage: plot_analysis.py <analysis_dir> <output.png>")
        return 2

    analysis_dir = sys.argv[1]
    output_png = sys.argv[2]

    waveform_csv = os.path.join(analysis_dir, "analysis_waveform.csv")
    f0_csv = os.path.join(analysis_dir, "analysis_f0.csv")
    segments_csv = os.path.join(analysis_dir, "analysis_segments.csv")
    meta_json = os.path.join(analysis_dir, "analysis_meta.json")

    wave_rows = load_waveform_csv(waveform_csv)
    f0_rows = load_f0_csv(f0_csv)
    seg_rows = load_segments_csv(segments_csv)
    duration = load_duration(meta_json, wave_rows, f0_rows, seg_rows)

    width, height = 2200, 1240
    left, right = 100, width - 60
    wave_top, wave_bottom = 90, 530
    f0_top, f0_bottom = 650, 1140

    img = Image.new("RGB", (width, height), (13, 16, 24))
    draw = ImageDraw.Draw(img)

    draw.rectangle([left, wave_top, right, wave_bottom], fill=(23, 28, 39), outline=(70, 85, 110), width=1)
    draw.rectangle([left, f0_top, right, f0_bottom], fill=(23, 28, 39), outline=(70, 85, 110), width=1)

    def tx(t: float) -> int:
        return int(left + (max(0.0, min(duration, t)) / duration) * (right - left))

    # Time grid
    grid_step = 1.0
    if duration > 40.0:
        grid_step = 2.0
    if duration > 80.0:
        grid_step = 5.0
    t = 0.0
    while t <= duration + 1e-9:
        x = tx(t)
        draw.line([x, wave_top, x, f0_bottom], fill=(38, 46, 62), width=1)
        draw.text((x + 2, f0_bottom + 6), f"{t:.0f}s", fill=(140, 160, 190))
        t += grid_step

    # Waveform: min/max envelope
    wave_mid = int((wave_top + wave_bottom) * 0.5)
    wave_half = (wave_bottom - wave_top) * 0.45
    draw.line([left, wave_mid, right, wave_mid], fill=(90, 104, 128), width=1)
    for time_s, mn, mx in wave_rows:
        x = tx(time_s)
        y1 = int(wave_mid - max(-1.0, min(1.0, mx)) * wave_half)
        y0 = int(wave_mid - max(-1.0, min(1.0, mn)) * wave_half)
        draw.line([x, y0, x, y1], fill=(96, 189, 255), width=1)

    # F0 axis range (log2 scale)
    voiced_hz = [hz for _, hz, voiced in f0_rows if voiced == 1 and hz > 0.0]
    if voiced_hz:
        min_hz = max(40.0, min(voiced_hz) * 0.9)
        max_hz = min(1400.0, max(voiced_hz) * 1.1)
    else:
        min_hz, max_hz = 60.0, 700.0
    if max_hz <= min_hz + 1e-6:
        max_hz = min_hz + 1.0
    min_log = math.log2(min_hz)
    max_log = math.log2(max_hz)
    log_span = max(max_log - min_log, 1e-6)

    def fy(hz: float) -> int:
        hz = max(min_hz, min(max_hz, hz))
        ratio = (math.log2(hz) - min_log) / log_span
        return int(f0_bottom - ratio * (f0_bottom - f0_top))

    # F0 guide lines
    for hz in (80, 110, 220, 440, 880):
        if hz < min_hz or hz > max_hz:
            continue
        y = fy(hz)
        draw.line([left, y, right, y], fill=(46, 58, 77), width=1)
        draw.text((left + 4, y - 12), f"{hz:.0f} Hz", fill=(150, 170, 196))

    # F0 curve
    prev = None
    for time_s, hz, voiced in f0_rows:
        if voiced != 1 or hz <= 0.0:
            prev = None
            continue
        p = (tx(time_s), fy(hz))
        if prev is not None:
            dt = abs(time_s - prev[2])
            if dt <= 0.06:
                draw.line([prev[0], prev[1], p[0], p[1]], fill=(255, 180, 72), width=2)
        prev = (p[0], p[1], time_s)

    # Segmentation boundaries
    for start_s, end_s in seg_rows:
        xs = tx(start_s)
        xe = tx(end_s)
        draw.line([xs, wave_top, xs, f0_bottom], fill=(255, 86, 86), width=1)
        draw.line([xe, wave_top, xe, f0_bottom], fill=(255, 86, 86), width=1)

    draw.text((left, 24), "MeloDick Analysis Overlay", fill=(230, 236, 248))
    draw.text((left, 48), f"Duration: {duration:.2f}s   Segments: {len(seg_rows)}   F0 Frames: {len(f0_rows)}", fill=(170, 188, 214))
    draw.text((left + 8, wave_top + 8), "Waveform (min/max envelope)", fill=(172, 218, 255))
    draw.text((left + 8, f0_top + 8), "F0 Curve (voiced only, log scale) + segmentation boundaries", fill=(255, 212, 160))

    os.makedirs(os.path.dirname(os.path.abspath(output_png)), exist_ok=True)
    img.save(output_png)
    print(f"wrote: {output_png}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
