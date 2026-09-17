#!/usr/bin/env python3
"""fixture sample generator for voidImageViewer smoke testing.

the anomaly generator (make_anomaly_samples.py) answers "does it survive
hostile input"; this one answers the other half - "does it show real
imagery": the smoke sweep's committed set carries both. every fixture
here is hand-encoded with the standard library only (the anomaly
generator's own discipline - the ci legs can regenerate the bytes
byte-identical anywhere), so nothing depends on an encoder library
version:

  fx_anim_bounce.gif    64x64, 8 frames, 16-color palette, 20cs delays,
                        NETSCAPE infinite loop - a square bouncing along
                        the diagonal, full-canvas frames
  fx_anim_fade.gif      48x48, 6 frames - a dot fading through a gray
                        ramp on sub-rectangle frames with a transparent
                        background index and disposal 2 (the compositing
                        contract, not just the full-redraw one)
  fx_still_rgba.png     32x32 truecolor+alpha gradient (the blend path)
  fx_still_24bpp.bmp    48x32 uncompressed classic (the gdi mainline)
  fx_still_qoi_rgb.qoi  32x16 rgb gradient (the fork's own decoder)
  fx_still_qoi_rgba.qoi 32x16 rgba with an alpha ramp (the qoi alpha path)
  fx_still_webp_odd.webp
                        101x101 vp8l still - a two-color weave behind
                        simple prefix codes, hand-encoded (the shape
                        dimension: a 24bpp dib frame with a non power of
                        two width is the exact figure the hardware
                        upload paths' gutter logic exists for, and the
                        golden set only ever sent it power-of-two widths)
  fx_still_qoi_sliver.qoi
                        1000x37 rgb bands, 8px blocks (the extreme
                        aspect: pot_high carries 27 rows of replicated
                        last row, pot_wide 24 columns of replicated
                        last column - the pad replication on both axes)
  fx_still_textured.png
                        130x97 truecolor discrimination fixture (the
                        fourth audit's finding: the golden set's two
                        controls are single solid fills - their role is
                        the scaling extremes, not filter discrimination,
                        and a solid fill can legitimately answer
                        identically under halftone and linear sampling.
                        this one carries structure at several scales: a
                        smooth double ramp, a 2px checkerboard on blue,
                        and hard marker bands - content that no scaling
                        or filter pass can answer identically on)

the gif lzw is a real variable-width compressor (dictionary growth, the
code-width lockstep the format owns); the self check decodes every frame
back through a mirror decoder and compares pixel-for-pixel. the qoi
encoder rides the reference semantics and the self check decodes both
qoi fixtures through a verbatim port of src/qoi.c's opcode walk (the
index hash, the diff and luma deltas, the run bias of -1, rgb chunks
preserving the running alpha, the trailing-chunk leniency). the vp8l
writer packs the container's own lsb-first bit order and the self check
walks the stream back through a mirror decoder (the header, the five
simple prefix codes, the literal pixel walk) and compares pixel for
pixel - the shape the encoder writes is the only shape the mirror
accepts, so a bit-order or field-width slip fails the round trip, not
some downstream consumer.

usage:  python tests/make_fixture_samples.py [output_dir]

the output directory defaults to tests/samples next to this script. a
clean run exits 0; any self check failure exits 1.
"""

import os
import struct
import sys
import zlib

PNG_SIG = b'\x89PNG\r\n\x1a\n'

FIXTURE_COUNT = 9


# ---------------------------------------------------------------- png tools

def png_chunk(tag, data):
    return (struct.pack('>I', len(data)) + tag + data +
            struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff))


def make_rgba_png(width, height):
    """a truecolor+alpha gradient: rgb ramps on x and y, alpha ramps on
    x - every row is real filtered scanline data (filter 0)."""
    comp = zlib.compressobj(9)
    body = bytearray()
    for y in range(height):
        row = bytearray(b'\x00')
        for x in range(width):
            row += bytes(((x * 8) & 0xff, (y * 8) & 0xff,
                          ((x * 2 + y * 4) & 0xff), (x * 8) & 0xff))
        body += comp.compress(bytes(row))
    body += comp.flush()
    ihdr = struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)
    return (PNG_SIG + png_chunk(b'IHDR', ihdr) +
            png_chunk(b'IDAT', bytes(body)) + png_chunk(b'IEND', b''))


# ---------------------------------------------------------------- bmp tools

def make_textured_png(width, height):
    """the discrimination fixture: a ramp on x, a ramp on y, a 2px
    checkerboard on blue and hard marker bands - real structure at
    several scales, so the golden set's three renderers have something
    to disagree about (a filter or resample pass that changes anything
    changes the bitmap)."""
    comp = zlib.compressobj(9)
    body = bytearray()
    band_x = width // 3
    band_y = height // 2
    for y in range(height):
        row = bytearray(b'\x00')
        for x in range(width):
            r = (x * 255) // max(width - 1, 1)
            g = (y * 255) // max(height - 1, 1)
            b = 0x40 if (((x >> 1) + (y >> 1)) & 1) else 0xc0
            if x == band_x or x == band_x + 1:
                r, g, b = 255, 255, 0
            if y == band_y:
                r, g, b = 0, 255, 255
            row += bytes((r, g, b))
        body += comp.compress(bytes(row))
    body += comp.flush()
    ihdr = struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)
    return (PNG_SIG + png_chunk(b'IHDR', ihdr) +
            png_chunk(b'IDAT', bytes(body)) + png_chunk(b'IEND', b''))


def make_24bpp_bmp(width, height):
    """an uncompressed 24-bit classic with a two-tone weave - real pixel
    data, not a solid fill (the anomaly set already has one of those)."""
    row = (width * 3 + 3) & ~3
    size = 54 + row * height
    out = b'BM' + struct.pack('<IHHI', size, 0, 0, 54)
    out += struct.pack('<IiiHHIIiiII', 40, width, height, 1, 24, 0,
                       row * height, 2835, 2835, 0, 0)
    body = bytearray()
    for y in range(height):
        for x in range(width):
            if ((x // 4) + (y // 4)) & 1:
                px = (0x30 + (x * 2) & 0x4f, 0x50, 0x90)
            else:
                px = (0xd0, 0xa0 - (y & 0x1f), 0x40)
            body += bytes(reversed(px))
        body += b'\x00' * (row - width * 3)
    return out + bytes(body)


# ---------------------------------------------------------------- qoi tools

QOI_END = b'\x00' * 7 + b'\x01'


def qoi_hash(r, g, b, a):
    return (r * 3 + g * 5 + b * 7 + a * 11) & 63


def make_qoi(width, height, channels, pixel_fn):
    """the reference encoder semantics: index slots, diff, luma, run
    chunks with the -1 bias, rgb chunks preserving the running alpha,
    rgba chunks when the alpha moves."""
    out = bytearray(b'qoif')
    out += struct.pack('>II', width, height)
    out.append(channels)
    out.append(0)          # srgb with a linear tag never changes the decode
    index = [(0, 0, 0, 255)] * 64
    pr, pg, pb, pa = 0, 0, 0, 255
    run = 0
    stream = bytearray()

    def flush_run():
        nonlocal run
        while run > 0:
            take = 62 if run > 62 else run
            stream.append(0xc0 | (take - 1))
            run -= take

    for y in range(height):
        for x in range(width):
            r, g, b, a = pixel_fn(x, y)
            if channels == 3:
                a = 255
            if (r, g, b, a) == (pr, pg, pb, pa):
                run += 1
                continue
            flush_run()
            h = qoi_hash(r, g, b, a)
            if index[h] == (r, g, b, a):
                stream.append(h)
            else:
                index[h] = (r, g, b, a)
                dr, dg, db = r - pr, g - pg, b - pb
                if channels == 4 and a != pa:
                    stream.append(0xff)
                    stream += bytes((r, g, b, a))
                elif -2 <= dr <= 1 and -2 <= dg <= 1 and -2 <= db <= 1:
                    stream.append(0x40 | ((dr + 2) << 4) |
                                  ((dg + 2) << 2) | (db + 2))
                elif -32 <= dg <= 31 and -8 <= dr - dg <= 7 \
                        and -8 <= db - dg <= 7:
                    stream.append(0x80 | (dg + 32))
                    stream.append(((dr - dg + 8) << 4) | (db - dg + 8))
                else:
                    stream.append(0xfe)
                    stream += bytes((r, g, b))
            pr, pg, pb, pa = r, g, b, a
    flush_run()
    out += stream
    out += QOI_END
    return bytes(out)


def qoi_decode(data):
    """a verbatim port of src/qoi.c's opcode walk (the shipped decoder's
    exact semantics, hostile-input discipline included)."""
    if len(data) < 14 + 8 or data[:4] != b'qoif':
        raise ValueError('not a qoi stream')
    width = struct.unpack('>I', data[4:8])[0]
    height = struct.unpack('>I', data[8:12])[0]
    channels = data[12]
    if data[-8:] != QOI_END:
        raise ValueError('missing the end marker')
    chunks_end = len(data) - 8
    index = [(0, 0, 0, 255)] * 64
    r, g, b, a = 0, 0, 0, 255
    run = 0
    p = 14
    out = []
    total = width * height
    while len(out) < total:
        if run:
            run -= 1
        elif p < chunks_end:
            b1 = data[p]
            p += 1
            if b1 == 0xfe:
                if chunks_end - p < 3:
                    raise ValueError('truncated rgb chunk')
                r, g, b = data[p], data[p + 1], data[p + 2]
                p += 3
            elif b1 == 0xff:
                if chunks_end - p < 4:
                    raise ValueError('truncated rgba chunk')
                r, g, b, a = data[p], data[p + 1], data[p + 2], data[p + 3]
                p += 4
            elif (b1 & 0xc0) == 0x00:
                r, g, b, a = index[b1 & 0x3f]
            elif (b1 & 0xc0) == 0x40:
                r = (r + ((b1 >> 4 & 0x03) - 2)) & 0xff
                g = (g + ((b1 >> 2 & 0x03) - 2)) & 0xff
                b = (b + ((b1 & 0x03) - 2)) & 0xff
            elif (b1 & 0xc0) == 0x80:
                if p >= chunks_end:
                    raise ValueError('truncated luma chunk')
                b2 = data[p]
                p += 1
                vg = (b1 & 0x3f) - 32
                r = (r + vg - 8 + ((b2 >> 4) & 0x0f)) & 0xff
                g = (g + vg) & 0xff
                b = (b + vg - 8 + (b2 & 0x0f)) & 0xff
            else:
                run = b1 & 0x3f
            index[qoi_hash(r, g, b, a)] = (r, g, b, a)
        else:
            raise ValueError('the input ran out before the canvas filled')
        out.append((r, g, b, a))
    return width, height, channels, out


# ---------------------------------------------------------------- vp8l tools

class Vp8lBits:
    """the vp8l bit order: fields pack lsb-first into little-endian
    bytes (the container's own rule - the first bit written lands in
    bit 0 of byte 0)."""

    def __init__(self):
        self.acc = 0
        self.nbits = 0
        self.out = bytearray()

    def put(self, value, count):
        for i in range(count):
            self.acc |= ((value >> i) & 1) << self.nbits
            self.nbits += 1
            if self.nbits == 8:
                self.out.append(self.acc)
                self.acc = 0
                self.nbits = 0

    def finish(self):
        if self.nbits:
            self.out.append(self.acc)
            self.acc = 0
            self.nbits = 0
        return bytes(self.out)


class Vp8lRead:
    """the mirror: the same lsb-first order, walked back."""

    def __init__(self, data):
        self.data = data
        self.pos = 0
        self.acc = 0
        self.nbits = 0

    def get(self, count):
        value = 0
        for i in range(count):
            if self.nbits == 0:
                self.acc = self.data[self.pos]
                self.pos += 1
                self.nbits = 8
            value |= (self.acc & 1) << i
            self.acc >>= 1
            self.nbits -= 1
        return value


def _vp8l_simple_code(w, symbols):
    """a simple prefix code: 1 bit simple flag, 1 bit num_symbols-1,
    1 bit first-symbol width, then the symbols as (value, width) pairs
    (8 bits when the width flag answers 8, 1 bit otherwise). a
    two-symbol code reads one bit per use (0 selects the first); a
    one-symbol code reads nothing."""
    if len(symbols) == 2 and symbols[1][1] != 8:
        raise ValueError('the format reads a second symbol at 8 bits, '
                         'whatever the first one cost')
    w.put(1, 1)
    w.put(len(symbols) - 1, 1)
    w.put(1 if symbols[0][1] == 8 else 0, 1)
    for value, width in symbols:
        w.put(value, width)


def _vp8l_read_simple_code(r):
    if r.get(1) != 1:
        raise ValueError('the mirror only reads simple codes')
    count = r.get(1) + 1
    wide = r.get(1)
    width = 8 if wide else 1
    symbols = [r.get(width) for _ in range(count)]
    return symbols


# the weave pair: every channel differs between the two colors and no
# channel value collides across the pair, so each of the three channel
# bits a pixel costs answers its own channel - a flipped bit is a wrong
# color in the hash, not a coincidence that still decodes.
VP8L_WEAVE_A = (0x28, 0x60, 0x98)
VP8L_WEAVE_B = (0xd8, 0xa8, 0x38)


def webp_weave_pixel(x, y):
    """4px blocks on the diagonal, the bmp weave's own granularity
    (opaque - the alpha answers nothing at these sizes)."""
    if ((x // 4) + (y // 4)) & 1:
        return VP8L_WEAVE_B + (255,)
    return VP8L_WEAVE_A + (255,)


def make_still_webp(width, height, pixel_fn):
    """a hand-encoded vp8l still, all literals: the header, no color
    cache, a single prefix-code group of five simple codes (green, red,
    blue carry the two weave colors; alpha carries one opaque symbol;
    the distance code exists because the group has five slots and never
    answers), then one green/red/blue bit per pixel. a decode of this
    stream never takes an lz77 copy, so the only variables the pixels
    carry are the ones the weave function owns. one caveat the mirror
    cannot see: with a two-color weave the green and red bits are
    equal at every pixel, so a green/red write-order transposition
    produces byte-identical output - the vendored-decoder host proof
    carries that permutation, and a third color would be the fixture
    that closes it.

    the channel pairs ride sorted ascending because the canonical
    assignment orders same-length codes by symbol value (the smaller
    value takes code 0 - the vendored huffman table build sorts, the
    stream order does not choose), and a single-symbol code builds a
    zero-bit table entry, so alpha and the distance code never cost a
    bit at a pixel."""
    w = Vp8lBits()
    w.put(0x2f, 8)                      # the vp8l signature
    w.put(width - 1, 14)
    w.put(height - 1, 14)
    w.put(0, 1)                         # alpha_is_used
    w.put(0, 3)                         # version
    w.put(0, 1)                         # transform: absent (the level-0
    #                                   # loop reads present bits until a
    #                                   # zero answers)
    w.put(0, 1)                         # color cache: absent
    w.put(0, 1)                         # meta huffman: single group
    pairs = [sorted((VP8L_WEAVE_A[1], VP8L_WEAVE_B[1])),
             sorted((VP8L_WEAVE_A[0], VP8L_WEAVE_B[0])),
             sorted((VP8L_WEAVE_A[2], VP8L_WEAVE_B[2]))]
    for pair in pairs:
        _vp8l_simple_code(w, [(value, 8) for value in pair])
    _vp8l_simple_code(w, [(255, 8)])
    _vp8l_simple_code(w, [(0, 1)])
    for y in range(height):
        for x in range(width):
            r, g, b, _a = pixel_fn(x, y)
            w.put(pairs[0].index(g), 1)
            w.put(pairs[1].index(r), 1)
            w.put(pairs[2].index(b), 1)
    payload = w.finish()
    chunk = b'VP8L' + struct.pack('<I', len(payload)) + payload
    if len(payload) & 1:                # riff chunks pad to even
        chunk += b'\x00'
    return b'RIFF' + struct.pack('<I', 4 + len(chunk)) + b'WEBP' + chunk


def vp8l_decode_still(data):
    """the mirror decoder: the exact shape the encoder writes, walked
    back bit for bit (header, five simple codes, the literal pixel
    walk) - any field-width or bit-order slip in the writer fails here
    instead of in some downstream consumer."""
    if data[:4] != b'RIFF' or data[8:12] != b'WEBP' or data[12:16] != b'VP8L':
        raise ValueError('not a riff/webp/vp8l container')
    if struct.unpack('<I', data[4:8])[0] != len(data) - 8:
        raise ValueError('riff size does not match the file')
    payload_size = struct.unpack('<I', data[16:20])[0]
    if payload_size > len(data) - 20:
        raise ValueError('vp8l chunk overruns the file')
    r = Vp8lRead(data[20:20 + payload_size])
    if r.get(8) != 0x2f:
        raise ValueError('vp8l signature missing')
    width = r.get(14) + 1
    height = r.get(14) + 1
    if r.get(1):
        raise ValueError('the mirror reads alpha-is-used = 0 only')
    if r.get(3) != 0:
        raise ValueError('vp8l version must be 0')
    if r.get(1):
        raise ValueError('the mirror reads no transform (the level-0 loop)')
    if r.get(1):
        raise ValueError('the mirror reads no color cache')
    if r.get(1):
        raise ValueError('the mirror reads a single code group')
    codes = [_vp8l_read_simple_code(r) for _ in range(5)]
    # the canonical rule the vendored table build owns: same-length
    # codes order by symbol value (the smaller takes code 0), the
    # stream order does not choose - a two-symbol code read back sorts
    # before any pixel bit indexes it.
    codes = [sorted(c) for c in codes]
    if len(codes[3]) != 1 or codes[3][0] != 255:
        raise ValueError('the alpha code must be the single opaque symbol')
    pixels = []
    for _ in range(width * height):
        g = codes[0][r.get(1)] if len(codes[0]) == 2 else codes[0][0]
        rr = codes[1][r.get(1)] if len(codes[1]) == 2 else codes[1][0]
        b = codes[2][r.get(1)] if len(codes[2]) == 2 else codes[2][0]
        pixels.append((rr, g, b, 255))
    return width, height, pixels


# ---------------------------------------------------------------- gif tools

def lzw_encode(min_code_size, data):
    """the real variable-width gif compressor: the dictionary grows, the
    code width widens when the next slot stops fitting, the table caps
    at 4096 (no further entries, which the format allows)."""
    clear_code = 1 << min_code_size
    eoi_code = clear_code + 1
    width = min_code_size + 1
    table = {bytes((i,)): i for i in range(clear_code)}
    next_code = eoi_code + 1
    acc = 0
    nbits = 0
    out = bytearray()

    def emit(code):
        nonlocal acc, nbits
        acc |= code << nbits
        nbits += width
        while nbits >= 8:
            out.append(acc & 0xff)
            acc >>= 8
            nbits -= 8

    emit(clear_code)
    cur = b''
    for byte in data:
        cand = cur + bytes((byte,))
        if cand in table:
            cur = cand
        else:
            emit(table[cur])
            if next_code < 4096:
                table[cand] = next_code
                next_code += 1
                # the encoder grows when the counter passes the code
                # space; the decoder (one entry behind) grows when its
                # counter reaches it - the lockstep the format owns.
                if next_code > (1 << width) and width < 12:
                    width += 1
            cur = bytes((byte,))
    if cur:
        emit(table[cur])
    emit(eoi_code)
    if nbits:
        out.append(acc & 0xff)
    return bytes(out)


def lzw_decode(min_code_size, packed):
    """the mirror decoder (the self check's oracle): one entry behind
    the encoder, grows the width when the counter reaches the space."""
    clear_code = 1 << min_code_size
    eoi_code = clear_code + 1
    width = min_code_size + 1
    acc = 0
    nbits = 0
    pos = 0
    out = bytearray()
    prev = None

    def read_code():
        nonlocal acc, nbits, pos
        while nbits < width:
            acc |= packed[pos] << nbits
            pos += 1
            nbits += 8
        code = acc & ((1 << width) - 1)
        acc >>= width
        nbits -= width
        return code

    table = {i: bytes((i,)) for i in range(clear_code)}
    next_code = eoi_code + 1
    while True:
        code = read_code()
        if code == clear_code:
            table = {i: bytes((i,)) for i in range(clear_code)}
            next_code = eoi_code + 1
            width = min_code_size + 1
            prev = None
            continue
        if code == eoi_code:
            break
        if prev is None:
            entry = table[code]
        else:
            if code in table:
                entry = table[code]
            elif code == next_code:
                entry = prev + prev[:1]
            else:
                raise ValueError('bad lzw code %d' % code)
            table[next_code] = prev + entry[:1]
            next_code += 1
            if next_code >= (1 << width) and width < 12:
                width += 1
        out += entry
        prev = entry
    return bytes(out)


def gif_sub_blocks(data):
    out = bytearray()
    for i in range(0, len(data), 255):
        block = data[i:i + 255]
        out.append(len(block))
        out += block
    out.append(0)
    return bytes(out)


def gif_gce(delay_cs, transparent_index=None, disposal=0):
    packed = (disposal & 7) << 2
    if transparent_index is not None:
        packed |= 1
    return (b'\x21\xf9\x04' + bytes([packed & 0xff]) +
            struct.pack('<H', delay_cs) +
            bytes([transparent_index if transparent_index is not None
                   else 0]) + b'\x00')


def gif_frame(left, top, width, height, min_code_size, indices):
    desc = b'\x2c' + struct.pack('<HHHH', left, top, width, height) + b'\x00'
    return desc + bytes([min_code_size]) + gif_sub_blocks(
        lzw_encode(min_code_size, bytes(indices)))


BOUNCE_COLORS = [
    (0x14, 0x14, 0x18), (0xe8, 0x4a, 0x3f), (0xe2, 0x63, 0x2a),
    (0xd8, 0x9b, 0x27), (0x8f, 0xb5, 0x40), (0x3f, 0x9e, 0x5f),
    (0x3d, 0x8f, 0x8f), (0x44, 0x6f, 0xc2), (0x6c, 0x4f, 0xc8),
    (0x9a, 0x4c, 0xc0), (0xc7, 0x43, 0xa0), (0xd4, 0x4e, 0x74),
    (0xce, 0x62, 0x50), (0x8c, 0x8c, 0x96), (0xb8, 0xb8, 0xc0),
    (0xf2, 0xf2, 0xf4),
]

FADE_GRAYS = [
    (0x00, 0x00, 0x00),                    # 0: transparent slot
    (0x2a, 0x2a, 0x2a), (0x3c, 0x3c, 0x3c), (0x50, 0x50, 0x50),
    (0x64, 0x64, 0x64), (0x7a, 0x7a, 0x7a), (0x90, 0x90, 0x90),
    (0xa6, 0xa6, 0xa6), (0xbd, 0xbd, 0xbd), (0xd4, 0xd4, 0xd4),
    (0xea, 0xea, 0xea), (0xf6, 0xf6, 0xf6), (0xff, 0xff, 0xff),
    (0x88, 0x88, 0x88), (0xcc, 0xcc, 0xcc), (0x44, 0x44, 0x44),
]


def make_anim_bounce():
    """64x64, 8 frames, 20cs each, infinite loop: a 12x12 square walking
    the diagonal, its color cycling the palette - every frame is a full
    canvas of real indices."""
    width = height = 64
    frames = []
    for i in range(8):
        pos = 4 + i * 6
        color = 1 + (i % 8)
        indices = bytearray()
        for y in range(height):
            for x in range(width):
                if pos <= x < pos + 12 and pos <= y < pos + 12:
                    indices.append(color)
                else:
                    indices.append(0)
        frames.append(bytes(indices))
    out = b'GIF89a' + struct.pack('<HHBBB', width, height, 0xf3, 0, 0)
    for c in BOUNCE_COLORS:
        out += bytes(c)
    out += b'\x21\xff\x0bNETSCAPE2.0\x03\x01' + struct.pack('<H', 0) + b'\x00'
    for indices in frames:
        out += gif_gce(20, None, 1)
        out += gif_frame(0, 0, width, height, 4, indices)
    out += b'\x3b'
    return out, frames


def make_anim_fade():
    """48x48, 6 frames, 15cs each: a 24x24 dot sweeping right on
    sub-rectangle frames, brightness ramping the gray palette, the rest
    of the canvas transparent with disposal 2 - the compositing
    contract, not the full-redraw one."""
    width = height = 48
    sprite = 24
    frames = []
    for i in range(6):
        left = 2 + i * 4
        top = 12
        gray = 1 + i * 2
        indices = bytearray()
        cx = cy = sprite // 2
        radius = 10
        for y in range(sprite):
            for x in range(sprite):
                dx, dy = x - cx, y - cy
                if dx * dx + dy * dy <= radius * radius:
                    indices.append(gray)
                else:
                    indices.append(0)
        frames.append((left, top, bytes(indices)))
    out = b'GIF89a' + struct.pack('<HHBBB', width, height, 0xf3, 0, 0)
    for c in FADE_GRAYS:
        out += bytes(c)
    out += b'\x21\xff\x0bNETSCAPE2.0\x03\x01' + struct.pack('<H', 0) + b'\x00'
    for left, top, indices in frames:
        out += gif_gce(15, 0, 2)
        out += gif_frame(left, top, sprite, sprite, 4, indices)
    out += b'\x3b'
    return out, frames


# ---------------------------------------------------------------- the set

def build(out_dir):
    os.makedirs(out_dir, exist_ok=True)
    files = []

    def emit(name, data):
        path = os.path.join(out_dir, name)
        with open(path, 'wb') as f:
            f.write(data)
        files.append((name, data))

    bounce, bounce_frames = make_anim_bounce()
    emit('fx_anim_bounce.gif', bounce)

    fade, fade_frames = make_anim_fade()
    emit('fx_anim_fade.gif', fade)

    emit('fx_still_rgba.png', make_rgba_png(32, 32))
    emit('fx_still_24bpp.bmp', make_24bpp_bmp(48, 32))

    def rgb_pixel(x, y):
        return ((x * 8) & 0xff, (y * 16) & 0xff,
                ((x + y) * 4) & 0xff, 255)

    def rgba_pixel(x, y):
        return ((x * 8) & 0xff, (y * 16) & 0xff,
                ((x + y) * 4) & 0xff, (x * 8) & 0xff)

    emit('fx_still_qoi_rgb.qoi', make_qoi(32, 16, 3, rgb_pixel))
    emit('fx_still_qoi_rgba.qoi', make_qoi(32, 16, 4, rgba_pixel))

    # the shape dimension: a 24bpp dib frame at a non power of two
    # width is the exact figure the hardware upload paths' gutter
    # logic exists for, and a sliver answers the extreme aspect. see
    # the set docstring for what each shape covers.
    emit('fx_still_webp_odd.webp', make_still_webp(101, 101, webp_weave_pixel))

    def sliver_pixel(x, y):
        u = x // 8
        return ((40 + u * 5 + y * 3) & 0xff,
                (90 + u * 2 + y * 4) & 0xff,
                (150 - u * 3 - y * 2) & 0xff, 255)

    emit('fx_still_qoi_sliver.qoi', make_qoi(1000, 37, 3, sliver_pixel))

    # the discrimination fixture: the fourth audit asked for content
    # the golden set's filters could not answer identically on. see
    # the set docstring.
    emit('fx_still_textured.png', make_textured_png(130, 97))

    return files, (bounce_frames, fade_frames)


# ------------------------------------------------------------- self check

def gif_walk(data):
    """a structural walk (the self check's second oracle): header, lsd,
    gct, extensions and image descriptors - returns frames as (left,
    top, w, h, min_code_size, subblock bytes, delay, transparent,
    disposal) plus loop presence."""
    if data[:6] != b'GIF89a':
        raise ValueError('not a gif89a')
    width, height, packed, _, _ = struct.unpack('<HHBBB', data[6:13])
    p = 13
    gct = []
    if packed & 0x80:
        n = 2 << (packed & 7)
        gct = [tuple(data[p + i * 3:p + i * 3 + 3]) for i in range(n)]
        p += n * 3
    frames = []
    loop = None
    delay = None
    transparent = None
    disposal = 0
    while p < len(data):
        b = data[p]
        if b == 0x21:                       # extension
            label = data[p + 1]
            if label == 0xf9:
                block = data[p + 3:p + 7]
                disposal = (block[0] >> 2) & 7
                transparent = block[3] if block[0] & 1 else None
                delay = struct.unpack('<H', block[1:3])[0]
                p += 8
            elif label == 0xff and data[p + 3:p + 14] == b'NETSCAPE2.0':
                # 21 FF 0B "NETSCAPE2.0" 03 01 <loop u16> 00 = 19 bytes
                loop = struct.unpack('<H', data[p + 16:p + 18])[0]
                p += 19
            else:                            # skip unknown sub-blocks
                p += 2
                while data[p]:
                    p += 1 + data[p]
                p += 1
        elif b == 0x2c:                      # image descriptor
            left, top, w, h, fpacked = struct.unpack('<HHHHB',
                                                     data[p + 1:p + 10])
            p += 10
            if fpacked & 0x80:               # local color table
                p += (2 << (fpacked & 7)) * 3
            mcs = data[p]
            p += 1
            blob = bytearray()
            while data[p]:
                blob += data[p + 1:p + 1 + data[p]]
                p += 1 + data[p]
            p += 1
            frames.append((left, top, w, h, mcs, bytes(blob), delay,
                           transparent, disposal, fpacked & 0x40))
        elif b == 0x3b:
            break
        else:
            raise ValueError('stray byte %02x at %d' % (b, p))
    return width, height, gct, frames, loop


def self_check(files, frames_data):
    failures = []
    by_name = {n: d for n, d in files}
    bounce_frames, fade_frames = frames_data

    def expect(cond, msg):
        if not cond:
            failures.append(msg)

    expect(len(files) == FIXTURE_COUNT,
           'expected %d fixtures, produced %d' % (FIXTURE_COUNT, len(files)))

    # the bounce gif: 8 full-canvas frames, 20cs, infinite loop, and the
    # lzw round trip through the mirror decoder is pixel-exact.
    w, h, gct, frames, loop = gif_walk(by_name['fx_anim_bounce.gif'])
    expect((w, h) == (64, 64), 'bounce dims %r' % ((w, h),))
    expect(len(frames) == 8, 'bounce frame count %d' % len(frames))
    expect(loop == 0, 'bounce loop %r' % (loop,))
    expect(len(gct) == 16, 'bounce gct size %d' % len(gct))
    for i, (left, top, fw, fh, mcs, blob, delay, tr, disp, _) \
            in enumerate(frames):
        expect((left, top, fw, fh) == (0, 0, 64, 64),
               'bounce frame %d geometry %r' % (i, (left, top, fw, fh)))
        expect(delay == 20, 'bounce frame %d delay %r' % (i, delay))
        expect(tr is None, 'bounce frame %d should not be transparent' % i)
        decoded = lzw_decode(mcs, blob)
        expect(decoded == bounce_frames[i],
               'bounce frame %d lzw round trip mismatch (%d vs %d bytes)'
               % (i, len(decoded), len(bounce_frames[i])))

    # the fade gif: 6 sub-rectangle frames with transparency, disposal
    # 2, 15cs, and the pixel-exact lzw round trip per frame.
    w, h, gct, frames, loop = gif_walk(by_name['fx_anim_fade.gif'])
    expect((w, h) == (48, 48), 'fade dims %r' % ((w, h),))
    expect(len(frames) == 6, 'fade frame count %d' % len(frames))
    expect(loop == 0, 'fade loop %r' % (loop,))
    for i, (left, top, fw, fh, mcs, blob, delay, tr, disp, _) \
            in enumerate(frames):
        expect((left, top, fw, fh) == (2 + i * 4, 12, 24, 24),
               'fade frame %d geometry %r' % (i, (left, top, fw, fh)))
        expect(delay == 15, 'fade frame %d delay %r' % (i, delay))
        expect(tr == 0, 'fade frame %d transparent index %r' % (i, tr))
        expect(disp == 2, 'fade frame %d disposal %r' % (i, disp))
        decoded = lzw_decode(mcs, blob)
        expect(decoded == fade_frames[i][2],
               'fade frame %d lzw round trip mismatch' % i)

    # the png: truecolor+alpha, dims, and the idat inflates to exactly
    # height rows of (1 + width * 4).
    data = by_name['fx_still_rgba.png']
    expect(data[:8] == PNG_SIG, 'png signature missing')
    w, h = struct.unpack('>II', data[16:24])
    expect((w, h) == (32, 32), 'png dims %r' % ((w, h),))
    depth, ctype = data[24], data[25]
    expect((depth, ctype) == (8, 6), 'png depth/type %r' % ((depth, ctype),))
    idat = bytearray()
    p = 8
    while p < len(data) - 8:
        size = struct.unpack('>I', data[p:p + 4])[0]
        tag = data[p + 4:p + 8]
        if tag == b'IDAT':
            idat += data[p + 8:p + 8 + size]
        p += 12 + size
    raw = zlib.decompress(bytes(idat))
    expect(len(raw) == h * (1 + w * 4),
           'png pixel stream length %d' % len(raw))

    # the bmp: magic, 24bpp, dims, row stride.
    data = by_name['fx_still_24bpp.bmp']
    expect(data[:2] == b'BM', 'bmp magic missing')
    fw, fh = struct.unpack('<ii', data[18:26])
    bpp = struct.unpack('<H', data[28:30])[0]
    expect((fw, fh, bpp) == (48, 32, 24), 'bmp shape %r' % ((fw, fh, bpp),))

    # the qoi pair: header contract, end marker, and the verbatim
    # decoder port returns the exact source pixels.
    for name, channels, pixel_fn in [
            ('fx_still_qoi_rgb.qoi', 3, lambda x, y: (
                (x * 8) & 0xff, (y * 16) & 0xff, ((x + y) * 4) & 0xff, 255)),
            ('fx_still_qoi_rgba.qoi', 4, lambda x, y: (
                (x * 8) & 0xff, (y * 16) & 0xff, ((x + y) * 4) & 0xff,
                (x * 8) & 0xff))]:
        data = by_name[name]
        expect(data[:4] == b'qoif', name + ' magic missing')
        expect(data[-8:] == QOI_END, name + ' end marker missing')
        expect(data[12] == channels, name + ' channels %d' % data[12])
        expect(data[13] == 0, name + ' colorspace %d' % data[13])
        w, h, ch, pixels = qoi_decode(data)
        expect((w, h, ch) == (32, 16, channels),
               name + ' header %r' % ((w, h, ch),))
        want = [pixel_fn(x, y) for y in range(h) for x in range(w)]
        expect(pixels == want, name + ' decode mismatch (%d pixels)' %
               sum(1 for a, b in zip(pixels, want) if a != b))

    # the vp8l still: the container contract, the 101x101 non power of
    # two shape, and the mirror decoder returns the exact weave pixels.
    data = by_name['fx_still_webp_odd.webp']
    w, h, pixels = vp8l_decode_still(data)
    expect((w, h) == (101, 101),
           'webp dims %r' % ((w, h),))
    want = [webp_weave_pixel(x, y)
            for y in range(h) for x in range(w)]
    expect(pixels == want, 'webp weave decode mismatch (%d pixels)' %
           sum(1 for a, b in zip(pixels, want) if a != b))
    expect(data[12:16] == b'VP8L',
           'the odd webp is the simple vp8l container (no vp8x wrapper)')

    # the qoi sliver: the extreme aspect decodes back through the same
    # verbatim opcode port, and the shape is the point (1000x37: the
    # hardware pads answer 1024x64 - gutters on both axes).
    data = by_name['fx_still_qoi_sliver.qoi']
    expect(data[:4] == b'qoif', 'sliver magic missing')
    expect(data[-8:] == QOI_END, 'sliver end marker missing')
    expect(data[12] == 3, 'sliver channels %d' % data[12])

    def sliver_pixel(x, y):
        u = x // 8
        return ((40 + u * 5 + y * 3) & 0xff,
                (90 + u * 2 + y * 4) & 0xff,
                (150 - u * 3 - y * 2) & 0xff, 255)

    w, h, ch, pixels = qoi_decode(data)
    expect((w, h, ch) == (1000, 37, 3),
           'sliver header %r' % ((w, h, ch),))
    want = [sliver_pixel(x, y) for y in range(h) for x in range(w)]
    expect(pixels == want, 'sliver decode mismatch (%d pixels)' %
           sum(1 for a, b in zip(pixels, want) if a != b))

    # the textured png: truecolor rgb, dims, the idat inflates to exactly
    # height rows of (1 + width * 3), and - the whole point - the decoded
    # content carries thousands of distinct pixel values (the solid-fill
    # controls the fourth audit flagged carry one). a fixture that
    # regresses into a solid fill fails here, not in a green golden run.
    data = by_name['fx_still_textured.png']
    expect(data[:8] == PNG_SIG, 'textured png signature missing')
    w, h = struct.unpack('>II', data[16:24])
    expect((w, h) == (130, 97), 'textured png dims %r' % ((w, h),))
    depth, ctype = data[24], data[25]
    expect((depth, ctype) == (8, 2), 'textured png depth/type %r' % ((depth, ctype),))
    idat = bytearray()
    p = 8
    while p < len(data) - 8:
        size = struct.unpack('>I', data[p:p + 4])[0]
        tag = data[p + 4:p + 8]
        if tag == b'IDAT':
            idat += data[p + 8:p + 8 + size]
        p += 8 + size + 4
    raw = zlib.decompress(bytes(idat))
    expect(len(raw) == h * (1 + w * 3),
           'textured png pixel stream length %d' % len(raw))
    distinct = set()
    band_y = h // 2
    for y in range(h):
        row = raw[y * (1 + w * 3):(y + 1) * (1 + w * 3)]
        for x in range(w):
            distinct.add(tuple(row[1 + x * 3:4 + x * 3]))
    expect(len(distinct) >= 5000,
           'textured png carries only %d distinct pixels (a solid fill '
           'has one - the discrimination contract died)' % len(distinct))
    expect(tuple(raw[band_y * (1 + w * 3) + 1:band_y * (1 + w * 3) + 4]) == (0, 255, 255),
           'textured png marker band missing')

    # size hygiene: fixtures stay small (the repo carries them now).
    for name, data in files:
        expect(len(data) < 32768, '%s grew past 32 kb (%d bytes)'
               % (name, len(data)))

    return failures


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), 'samples')
    files, frames_data = build(out_dir)
    failures = self_check(files, frames_data)
    print('generated %d fixtures in %s' % (len(files), out_dir))
    for name, data in files:
        print('  %-28s %8d bytes' % (name, len(data)))
    if failures:
        for f in failures:
            print('SELF CHECK FAIL: ' + f)
        sys.exit(1)
    print('SELF CHECK PASS: %d fixtures, lzw, qoi and vp8l round trips exact'
          % FIXTURE_COUNT)
    sys.exit(0)


if __name__ == '__main__':
    main()
