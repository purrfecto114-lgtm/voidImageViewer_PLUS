#!/usr/bin/env python3
"""media fixture generator for voidImageViewer smoke testing.

authoring tool (local): the two fixtures that need a real codec library
- a photographic jpeg and an animated webp - generate here once and
commit; the checked-in bytes are the pinned truth. the ci legs never
run this script (pillow is not stdlib and the repo's test discipline
keeps the ci path pure stdlib - tests/make_fixture_samples.py carries
the hand-encoded six); the round-98 guard pins these two by structure
(magic, markers, dimensions, frame counts), not by regeneration.

usage:  python tools/gen_media_samples.py [output_dir]

the output directory defaults to tests/samples at the repo root. the
script refuses to overwrite differing bytes silently - it prints the
sha256 of what it writes so a regen under a different pillow or
libwebp version can be eyeballed before anything is committed.
"""

import hashlib
import os
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit('this authoring tool needs pillow locally; the ci path '
             'never runs it (tests/make_fixture_samples.py is the '
             'stdlib one).')


def photo_pixels(width, height):
    """a deterministic photographic-ish field: smooth two-lobe gradient
    plus a checker overlay and a soft vignette (real neighbors, not
    runs - the kind of content jpeg is for)."""
    px = Image.new('RGB', (width, height))
    for y in range(height):
        for x in range(width):
            u = x / (width - 1)
            v = y / (height - 1)
            r = int(60 + 120 * u)
            g = int(40 + 90 * v)
            b = int(150 - 60 * (u + v) / 2)
            if ((x // 6) + (y // 6)) & 1:
                r = min(255, r + 24)
                g = min(255, g + 18)
            cx, cy = 0.5, 0.5
            d = ((u - cx) ** 2 + (v - cy) ** 2) ** 0.5
            fade = max(0.0, 1.0 - d * 1.2)
            px.putpixel((x, y), (int(r * fade + 30 * (1 - fade)),
                                 int(g * fade + 20 * (1 - fade)),
                                 int(b * fade + 50 * (1 - fade))))
    return px


def make_photo_jpeg():
    return photo_pixels(96, 64)


def make_anim_webp():
    """64x64, 6 frames, 150 ms each, infinite loop: a ring pulsing its
    radius and hue - the animated webp path the vendored libwebp owns."""
    frames = []
    for i in range(6):
        im = Image.new('RGB', (64, 64), (0x10, 0x12, 0x18))
        radius = 12 + i * 4
        shade = int(90 + i * 25)
        cx = cy = 32
        for y in range(64):
            for x in range(64):
                d = ((x - cx) ** 2 + (y - cy) ** 2) ** 0.5
                if abs(d - radius) < 2.5:
                    im.putpixel((x, y), (shade, shade // 2, 255 - shade))
                elif d < radius - 3:
                    im.putpixel((x, y), (shade // 3, shade // 4, 0x18))
        frames.append(im)
    return frames


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), '..', 'tests',
        'samples')
    out_dir = os.path.abspath(out_dir)
    os.makedirs(out_dir, exist_ok=True)

    outputs = []

    im = make_photo_jpeg()
    p = os.path.join(out_dir, 'fx_still_photo.jpg')
    im.save(p, 'JPEG', quality=88)
    outputs.append(p)

    frames = make_anim_webp()
    p = os.path.join(out_dir, 'fx_anim_pulse.webp')
    frames[0].save(p, 'WEBP', save_all=True, append_images=frames[1:],
                   duration=150, loop=0, quality=85, method=4)
    outputs.append(p)

    for p in outputs:
        data = open(p, 'rb').read()
        print('%-28s %8d bytes  sha256 %s'
              % (os.path.basename(p), len(data),
                 hashlib.sha256(data).hexdigest()[:16]))
    print('wrote %d media fixtures to %s' % (len(outputs), out_dir))
    print('(regenerating under a different pillow/libwebp version may '
          'produce different bytes - compare before committing)')


if __name__ == '__main__':
    main()
