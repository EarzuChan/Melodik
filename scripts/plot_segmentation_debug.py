#!/usr/bin/env python3
import csv
import json
import math
import os
import sys
from dataclasses import dataclass
from typing import List, Tuple

from PIL import Image, ImageDraw, ImageFont


PITCH_JUMP_SEMITONES = 2.8
MAX_LEADING_UNVOICED_SECONDS = 0.12


@dataclass
class F0Frame:
    time_s: float
    midi: float
    hz: float
    voiced: bool


@dataclass
class Span:
    start_s: float
    end_s: float
    voiced: bool

    @property
    def duration_s(self) -> float:
        return max(0.0, self.end_s - self.start_s)


def load_waveform_csv(path: str) -> List[Tuple[float, float, float]]:
    rows = []
    with open(path, "r", encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f)
        for r in reader:
            rows.append((float(r["time_s"]), float(r["min_amp"]), float(r["max_amp"])))
    return rows


def load_f0_csv(path: str) -> List[F0Frame]:
    rows = []
    with open(path, "r", encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f)
        for r in reader:
            rows.append(
                F0Frame(
                    time_s=float(r["time_s"]),
                    midi=float(r["midi"]),
                    hz=float(r["hz"]),
                    voiced=int(r["voiced"]) == 1,
                )
            )
    return rows


def load_segments_csv(path: str) -> List[Span]:
    rows = []
    with open(path, "r", encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f)
        for r in reader:
            rows.append(
                Span(
                    start_s=float(r["start_s"]),
                    end_s=float(r["end_s"]),
                    voiced=float(r["display_hz"]) > 0.0,
                )
            )
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
        duration = max(duration, f0_rows[-1].time_s)
    for span in seg_rows:
        duration = max(duration, span.end_s)
    return max(duration, 1.0)


def build_premerge_spans(frames: List[F0Frame]) -> List[Span]:
    if not frames:
        return []

    state_runs = []
    run_start = 0
    for i in range(1, len(frames)):
        if frames[i].voiced == frames[i - 1].voiced:
            continue
        state_runs.append((run_start, i - 1, frames[i - 1].voiced))
        run_start = i
    state_runs.append((run_start, len(frames) - 1, frames[-1].voiced))

    spans = []
    for start, end, voiced in state_runs:
        if not voiced:
            spans.append(Span(frames[start].time_s, frames[end].time_s, False))
            continue

        voiced_start = start
        for i in range(start + 1, end + 1):
            if abs(frames[i].midi - frames[i - 1].midi) < PITCH_JUMP_SEMITONES:
                continue
            spans.append(Span(frames[voiced_start].time_s, frames[i - 1].time_s, True))
            voiced_start = i
        spans.append(Span(frames[voiced_start].time_s, frames[end].time_s, True))
    return spans


def build_postmerge_spans(premerge_spans: List[Span]) -> List[Span]:
    spans = [Span(s.start_s, s.end_s, s.voiced) for s in premerge_spans]
    merged = []
    i = 0
    while i < len(spans):
        span = spans[i]
        if (
            not span.voiced
            and i + 1 < len(spans)
            and spans[i + 1].voiced
            and span.duration_s <= MAX_LEADING_UNVOICED_SECONDS
        ):
            spans[i + 1].start_s = span.start_s
            i += 1
            continue
        merged.append(span)
        i += 1
    return merged


def choose_grid_step(duration: float) -> float:
    if duration > 80.0:
        return 5.0
    if duration > 40.0:
        return 2.0
    return 1.0


def fmt_duration(seconds: float) -> str:
    return f"{seconds:.3f}s"


def draw_segmentation_track(
    draw: ImageDraw.ImageDraw,
    spans: List[Span],
    track_box: Tuple[int, int, int, int],
    tx,
    label: str,
    font,
    small_font,
):
    left, top, right, bottom = track_box
    draw.rectangle(track_box, fill=(23, 28, 39), outline=(70, 85, 110), width=1)
    draw.text((left + 8, top + 8), label, fill=(230, 236, 248), font=font)

    lane_top = top + 42
    lane_bottom = bottom - 16
    lane_mid = (lane_top + lane_bottom) // 2
    lane_height = lane_bottom - lane_top

    for idx, span in enumerate(spans):
        x0 = tx(span.start_s)
        x1 = max(x0 + 1, tx(span.end_s))
        fill = (80, 162, 255) if span.voiced else (140, 146, 160)
        outline = (184, 224, 255) if span.voiced else (210, 210, 210)
        rect = [x0, lane_top, x1, lane_bottom]
        draw.rectangle(rect, fill=fill, outline=outline, width=1)
        draw.line([x0, top, x0, bottom], fill=(255, 92, 92), width=1)
        draw.line([x1, top, x1, bottom], fill=(255, 92, 92), width=1)

        duration_text = fmt_duration(span.duration_s)
        bbox = draw.textbbox((0, 0), duration_text, font=small_font)
        text_w = bbox[2] - bbox[0]
        text_h = bbox[3] - bbox[1]
        text_x = max(left + 2, min((x0 + x1 - text_w) // 2, right - text_w - 2))
        lane_index = idx % 3
        text_y = top + 10 + lane_index * (text_h + 2)
        bg = [text_x - 2, text_y - 1, text_x + text_w + 2, text_y + text_h + 1]
        draw.rectangle(bg, fill=(12, 15, 23))
        draw.text((text_x, text_y), duration_text, fill=(255, 225, 170), font=small_font)

    if spans:
        draw.line([tx(spans[-1].end_s), top, tx(spans[-1].end_s), bottom], fill=(255, 92, 92), width=1)
    draw.line([left, lane_mid, right, lane_mid], fill=(46, 58, 77), width=1)


def main():
    if len(sys.argv) < 3:
        print("Usage: plot_segmentation_debug.py <analysis_dir> <output.png>")
        return 2

    analysis_dir = sys.argv[1]
    output_png = sys.argv[2]

    waveform_csv = os.path.join(analysis_dir, "analysis_waveform.csv")
    f0_csv = os.path.join(analysis_dir, "analysis_f0.csv")
    segments_csv = os.path.join(analysis_dir, "analysis_segments.csv")
    meta_json = os.path.join(analysis_dir, "analysis_meta.json")

    wave_rows = load_waveform_csv(waveform_csv)
    f0_rows = load_f0_csv(f0_csv)
    final_spans = load_segments_csv(segments_csv)
    duration = load_duration(meta_json, wave_rows, f0_rows, final_spans)

    premerge_spans = build_premerge_spans(f0_rows)
    merged_preview_spans = build_postmerge_spans(premerge_spans)

    width, height = 4200, 1320
    left, right = 120, width - 80
    wave_top, wave_bottom = 100, 430
    pre_top, pre_bottom = 520, 860
    post_top, post_bottom = 940, 1280

    img = Image.new("RGB", (width, height), (13, 16, 24))
    draw = ImageDraw.Draw(img)
    font = ImageFont.load_default()
    small_font = ImageFont.load_default()

    def tx(t: float) -> int:
        return int(left + (max(0.0, min(duration, t)) / duration) * (right - left))

    grid_step = choose_grid_step(duration)
    t = 0.0
    while t <= duration + 1e-9:
        x = tx(t)
        draw.line([x, wave_top, x, post_bottom], fill=(38, 46, 62), width=1)
        draw.text((x + 3, post_bottom + 8), f"{t:.0f}s", fill=(150, 170, 196), font=font)
        t += grid_step

    draw.rectangle([left, wave_top, right, wave_bottom], fill=(23, 28, 39), outline=(70, 85, 110), width=1)
    draw.text((left + 8, wave_top + 8), "Waveform (min/max envelope)", fill=(172, 218, 255), font=font)

    wave_mid = int((wave_top + wave_bottom) * 0.5)
    wave_half = (wave_bottom - wave_top) * 0.43
    draw.line([left, wave_mid, right, wave_mid], fill=(90, 104, 128), width=1)
    for time_s, mn, mx in wave_rows:
        x = tx(time_s)
        y1 = int(wave_mid - max(-1.0, min(1.0, mx)) * wave_half)
        y0 = int(wave_mid - max(-1.0, min(1.0, mn)) * wave_half)
        draw.line([x, y0, x, y1], fill=(96, 189, 255), width=1)

    draw_segmentation_track(
        draw,
        premerge_spans,
        (left, pre_top, right, pre_bottom),
        tx,
        f"Segmentation Before Merge   spans={len(premerge_spans)}   pitch_jump={PITCH_JUMP_SEMITONES:.1f}   leading_unvoiced_merge<= {MAX_LEADING_UNVOICED_SECONDS:.3f}s",
        font,
        small_font,
    )
    draw_segmentation_track(
        draw,
        final_spans,
        (left, post_top, right, post_bottom),
        tx,
        f"Segmentation After Merge (Current Export)   spans={len(final_spans)}   merged_preview={len(merged_preview_spans)}",
        font,
        small_font,
    )

    draw.text((left, 24), "MeloDick Segmentation Debug View", fill=(230, 236, 248), font=font)
    draw.text(
        (left, 48),
        f"Duration: {duration:.2f}s   F0 Frames: {len(f0_rows)}   Exported Segments: {len(final_spans)}   Pre-Merge Preview: {len(premerge_spans)}",
        fill=(170, 188, 214),
        font=font,
    )

    os.makedirs(os.path.dirname(os.path.abspath(output_png)), exist_ok=True)
    img.save(output_png)
    print(f"wrote: {output_png}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
