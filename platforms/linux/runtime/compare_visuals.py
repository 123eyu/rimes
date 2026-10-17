#!/usr/bin/env python3
"""Compare actual native renders. Differences are evidence, never an auto-pass."""
import argparse
import html
import json
from io import BytesIO
from pathlib import Path
from PIL import Image, ImageChops, ImageCms, ImageStat


def srgb(image):
    # AppKit may embed the active monitor profile; Cairo PNGs use sRGB.
    profile = image.info.get("icc_profile")
    if profile:
        return ImageCms.profileToProfile(image.convert("RGB"),
            ImageCms.ImageCmsProfile(BytesIO(profile)), ImageCms.createProfile("sRGB"), outputMode="RGB")
    return image.convert("RGB")


def compare(reference, candidate, output):
    output.mkdir(parents=True, exist_ok=True)
    rows = []
    manifest = []
    for linux_file in sorted(candidate.glob("*.png")):
        native_file = reference / linux_file.name.replace("core.", "core-")
        if not native_file.is_file():
            manifest.append({"page": linux_file.stem, "status": "missing_reference"})
            continue
        with Image.open(native_file) as native, Image.open(linux_file) as linux:
            if native.size != linux.size:
                manifest.append({"page": linux_file.stem, "status": "size_mismatch",
                                 "macos": native.size, "linux": linux.size})
                continue
            native, linux = srgb(native), srgb(linux)
            difference = ImageChops.difference(native, linux)
            maximum = ImageChops.lighter(ImageChops.lighter(*difference.split()[:2]), difference.split()[2])
            count = sum(maximum.histogram()[1:])
            total = native.width * native.height
            pair = Image.new("RGB", (native.width * 2, native.height))
            pair.paste(native, (0, 0)); pair.paste(linux, (native.width, 0))
            pair_name, diff_name = linux_file.stem + "-pair.png", linux_file.stem + "-diff.png"
            pair.save(output / pair_name)
            difference.point(lambda value: min(value * 4, 255)).save(output / diff_name)
            record = {"page": linux_file.stem, "status": "requires_review",
                      "comparison_color_space": "sRGB (embedded ICC profiles converted)",
                      "size": native.size, "changed_pixel_fraction": count / total,
                      "mean_absolute_channel_error": sum(ImageStat.Stat(difference).mean) / 3,
                      "pair": pair_name, "difference_4x": diff_name}
            manifest.append(record)
            rows.append(f'<section><h2>{html.escape(linux_file.stem)}</h2>'
                        f'<p>macOS 左 / Linux 右 · 变化像素 {count / total:.2%} · 需逐项审阅</p>'
                        f'<img src="{pair_name}" alt="双端原始截图并排">'
                        f'<details><summary>像素差异（亮度放大 4 倍）</summary>'
                        f'<img src="{diff_name}" alt="像素差异"></details></section>')
    (output / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
    (output / "index.html").write_text('<!doctype html><html lang="zh-CN"><meta charset="utf-8">'
        '<title>macOS / Linux 原生界面对照</title><style>body{font:15px system-ui;background:#202124;'
        'color:#eee;max-width:1600px;margin:32px auto}img{width:100%;height:auto}section{margin:40px 0}'
        '</style><h1>macOS / Linux 原生界面对照</h1>'
        '<p>此报告不自动判定通过。先核对数据、状态、主题与缩放，再审阅布局和像素差异；'
        '相似度不能代替交互测试。</p>' + ''.join(rows) + '</html>')
    return manifest


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--macos", required=True, type=Path)
    parser.add_argument("--linux", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    if not args.macos.is_dir() or not args.linux.is_dir():
        parser.error("both native render directories must exist")
    records = compare(args.macos, args.linux, args.output)
    print(json.dumps({"pages": len(records), "review_required": True}))
