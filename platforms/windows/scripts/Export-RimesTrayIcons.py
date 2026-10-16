"""Export Windows tray sizes from the shared macOS horn. Requires Pillow."""

import re
from pathlib import Path
from xml.etree import ElementTree

from PIL import Image, ImageChops, ImageDraw

ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / "Logo/rhino-horn.svg"
OUTPUT = ROOT / "platforms/windows/native/resources"
SIZES = [(size, size) for size in (16, 20, 24, 32, 40, 48, 64)]


def horn_mask():
    path = ElementTree.parse(SOURCE).getroot().find("{http://www.w3.org/2000/svg}path")
    tokens = iter(re.findall(r"[A-Za-z]|-?\d+(?:\.\d+)?", path.attrib["d"]))
    mask = Image.new("1", (512, 512))
    points = []
    for command in tokens:
        if command == "M":
            points = [(float(next(tokens)), float(next(tokens)))]
        elif command == "C":
            start = points[-1]
            controls = [(float(next(tokens)), float(next(tokens))) for _ in range(3)]
            for index in range(1, 65):
                t = index / 64
                points.append(tuple(
                    (1 - t) ** 3 * start[axis]
                    + 3 * (1 - t) ** 2 * t * controls[0][axis]
                    + 3 * (1 - t) * t ** 2 * controls[1][axis]
                    + t ** 3 * controls[2][axis]
                    for axis in range(2)
                ))
        elif command == "Z":
            contour = Image.new("1", mask.size)
            ImageDraw.Draw(contour).polygon([(x * 32, y * 32) for x, y in points], fill=1)
            mask = ImageChops.logical_xor(mask, contour)  # SVG evenodd fill
        else:
            raise ValueError(f"Unsupported SVG path command: {command}")
    return mask.convert("L")


if __name__ == "__main__":
    alpha = horn_mask()
    for name, color in (("black", "black"), ("white", "white")):
        icon = Image.new("RGBA", alpha.size, color)
        icon.putalpha(alpha)
        icon = icon.resize((64, 64), Image.Resampling.LANCZOS)
        destination = OUTPUT / f"rhino-horn-{name}.ico"
        icon.save(destination, sizes=SIZES)
        with Image.open(destination) as check:
            assert check.ico.sizes() == set(SIZES)
        print(destination)

    # TSF profile icons are static resources: Windows does not select our
    # tray's light/dark variant. A dark badge with a light rim and white horn
    # remains legible on both taskbar themes, including 16-pixel indicators.
    badge = Image.new("RGBA", (512, 512))
    ImageDraw.Draw(badge).rounded_rectangle((8, 8, 503, 503), radius=104,
        fill=(35, 35, 39, 255), outline=(245, 245, 248, 255), width=24)
    inset = alpha.resize((384, 384), Image.Resampling.LANCZOS)
    badge.paste((255, 255, 255, 255), (64, 64), inset)
    destination = OUTPUT / "rhino-horn-contrast.ico"
    badge.resize((64, 64), Image.Resampling.LANCZOS).save(destination, sizes=SIZES)
    with Image.open(destination) as check:
        assert check.ico.sizes() == set(SIZES)
    print(destination)
