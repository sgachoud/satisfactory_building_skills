"""Render original geometric publication artwork. Requires Pillow; no game assets."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "docs" / "publishing"
BG, INK, GREEN, PURPLE = "#111e28", "#eaf3f6", "#5ce6a2", "#b0a0ff"


def icon(size=512):
    scale = 4
    image = Image.new("RGB", (512 * scale, 512 * scale), BG)
    d = ImageDraw.Draw(image)
    svg = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512">'
           f'<rect width="512" height="512" rx="48" fill="{BG}"/>']

    def line(points, color, width):
        d.line([(x * scale, y * scale) for x, y in points],
               fill=color, width=width * scale, joint="curve")
        svg.append('<polyline points="' + " ".join(f"{x},{y}" for x, y in points)
                   + f'" fill="none" stroke="{color}" stroke-width="{width}"'
                     ' stroke-linejoin="round"/>')

    def poly(points, color):
        d.polygon([(x * scale, y * scale) for x, y in points], fill=color)
        svg.append('<polygon points="' + " ".join(f"{x},{y}" for x, y in points)
                   + f'" fill="{color}"/>')

    for offset in range(-256, 513, 64):
        line([(32, offset + 128), (480, offset + 352)], "#1c303c", 1)
        line([(32, offset + 352), (480, offset + 128)], "#1c303c", 1)
    # Three faces, keeping the block legible at small sizes.
    poly([(256, 122), (368, 180), (256, 240), (144, 180)], GREEN)
    poly([(144, 180), (256, 240), (256, 362), (144, 302)], "#278766")
    poly([(256, 240), (368, 180), (368, 302), (256, 362)], "#3bbd8b")
    line([(256, 240), (256, 362)], "#a2f8cf", 3)
    line([(144, 180), (256, 240), (368, 180)], "#a2f8cf", 3)
    # Selection corners.
    for points in [
        [(68, 164), (68, 76), (156, 76)], [(356, 76), (444, 76), (444, 164)],
        [(68, 348), (68, 436), (156, 436)], [(356, 436), (444, 436), (444, 348)],
    ]:
        line(points, GREEN, 10)
    # Two parallel planning curves.
    for lift in (0, 15):
        points = []
        for n in range(101):
            t = n / 100
            x = (1-t)**3*98 + 3*(1-t)**2*t*240 + 3*(1-t)*t*t*350 + t**3*414
            y = (1-t)**3*376 + 3*(1-t)**2*t*475 + 3*(1-t)*t*t*364 + t**3*306
            points.append((round(x, 3), round(y - lift, 3)))
        line(points, PURPLE, 5)
    svg.append("</svg>")
    (OUT / "icon.svg").write_text("\n".join(svg), encoding="utf-8")
    return image.resize((size, size), Image.Resampling.LANCZOS)


def font(size, bold=False):
    filename = "segoeuib.ttf" if bold else "segoeui.ttf"
    # Pillow also resolves installed fonts by name on Windows.
    return ImageFont.truetype(filename, size)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    logo = icon()
    logo.save(OUT / "icon-512.png")
    logo.resize((128, 128), Image.Resampling.LANCZOS).save(
        ROOT / "mod/BuildingSkills/Resources/Icon128.png")
    banner = Image.new("RGB", (1600, 900), BG)
    d = ImageDraw.Draw(banner)
    for x in range(0, 1600, 80):
        d.line([(x, 0), (x, 900)], fill="#192b36", width=1)
    for y in range(0, 900, 80):
        d.line([(0, y), (1600, y)], fill="#192b36", width=1)
    banner.paste(logo, (82, 174))
    d.text((674, 228), "BUILDING", font=font(80, True), fill=INK)
    d.text((674, 319), "SKILLS", font=font(104, True), fill=GREEN)
    d.text((680, 468), "Edit your factory.", font=font(40), fill=INK)
    d.text((680, 526), "Plan what comes next.", font=font(40), fill=INK)
    d.line([(100, 744), (1500, 744)], fill="#365261", width=2)
    for x, label in [(100, "GROUP"), (388, "TRANSFORM"), (760, "COPY"), (1045, "PLAN RAILS")]:
        d.text((x, 786), label, font=font(28, True), fill=PURPLE)
    banner.save(OUT / "banner-1600x900.png")
    print(f"Created original icon and banner in {OUT}")


if __name__ == "__main__":
    main()
