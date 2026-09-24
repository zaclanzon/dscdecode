#!/usr/bin/env python3
"""Hand-specified DSC 1.1 fixtures at 10 and 12 bits per component.

A restricted syntax constructor, NOT an encoder. It writes three kinds of
fixtures for each bit depth, from DSC 1.1 §4.4, §4.5, §6.1, §6.4.1, §6.4.3,
§6.6, §6.8 and Table 6-2:

  hbd<bpc>_color  lossless QP 0 colour picture, 2x2 slices of 50x3, cropped to
                  99x5; MMAP on first and later lines, MPP groups, partial
                  groups. Expected output: the source pixels.
  hbd<bpc>_ich    flat colour; an ICH escape and ICH groups on three lines.
  hbd<bpc>_qp     one 48x1 line of MPP groups at QP 0, 0, then a pinned QP
                  (13 at 10 bpc, 21 at 12 bpc) whose qLevels differ from the
                  8 bpc row of the same QP.

Expected pixels are written from the formulas derived by hand in
research/hbd-worked-note.md, never by a decoder. No reference-model source or
executable is used.
"""
from pathlib import Path
import hashlib
import json

from make_vectors import clamp

OUT = Path(__file__).parent / 'fixtures'


def signed_size(v):
    """Bits needed for v in two's complement; zero needs none (§6.6.1)."""
    if v == 0:
        return 0
    w = 1
    while not -(1 << (w - 1)) <= v < (1 << (w - 1)):
        w += 1
    return w


class Format:
    """DSC 1.1 RGB at a bit depth: sample bits (§6.1), mux word (§4.4) and
    the maximum syntax element size of each substream (4 * bpc + 4 for luma,
    4 * cpntBitDepth for chroma; see research/hbd-worked-note.md)."""

    def __init__(self, bpc):
        self.bpc = bpc
        self.depth = (bpc, bpc + 1, bpc + 1)
        self.mux = 48 if bpc <= 10 else 64
        self.max_se = (4 * bpc + 4, 4 * (bpc + 1), 4 * (bpc + 1))
        self.max_qp = 15 + 2 * (bpc - 8)


def multiplex(units, fmt, capacity):
    """Table 4-6: before each group, one mux word per substream below its
    maximum syntax element size; zero padding to capacity bytes."""
    streams = [''.join(u[c] for u in units) for c in range(3)]
    offset, fullness, words = [0] * 3, [0] * 3, []
    for u in units:
        for c in range(3):
            if fullness[c] < fmt.max_se[c]:
                words.append(streams[c][offset[c]:offset[c] + fmt.mux].ljust(fmt.mux, '0'))
                offset[c] += fmt.mux
                fullness[c] += fmt.mux
        for c in range(3):
            fullness[c] -= len(u[c])
            assert fullness[c] >= 0
    raw = ''.join(words)
    assert len(raw) <= capacity * 8, (len(raw), capacity * 8)
    padded = raw.ljust(capacity * 8, '0')
    return int(padded, 2).to_bytes(len(padded) // 8, 'big'), len(raw)


def pps(fmt, width, height, sw, sh, qp=0, bpp16=384):
    """24 bpp; an initial_xmit_delay of 512 pixels, longer than these slices,
    so no bits are removed; zero BPG offsets and final_offset, so the model
    fullness only falls; every range pinned to one QP; flatness signaled
    only at the largest QP, which these fixtures never reach."""
    p = bytearray(128)

    def word(i, v):
        p[i:i + 2] = v.to_bytes(2, 'big')
    chunk = (sw * bpp16 + 127) // 128
    p[0] = 0x11                                         # DSC 1.1
    p[3] = (fmt.bpc << 4) | (fmt.bpc + 1)               # line buffer bpc + 1
    p[4] = 0x10 | (bpp16 >> 8)                          # convert_rgb; no BP, CBR
    p[5] = bpp16 & 0xff
    for i, v in [(6, height), (8, width), (10, sh), (12, sw), (14, chunk),
                 (16, 512), (18, 512), (32, 2048), (38, 8192)]:
        word(i, v)
    p[21] = 8                                           # initial_scale_value 1.0
    p[36] = p[37] = fmt.max_qp                          # flatness only at the top QP
    p[40], p[41], p[42], p[43] = 6, fmt.max_qp, fmt.max_qp, 0x33
    p[44:58] = bytes([14, 28, 42, 56, 70, 84, 98, 105, 112, 119, 121, 123, 125, 126])
    for i in range(15):
        word(58 + 2 * i, (qp << 11) | (qp << 6))
    return bytes(p)


def forward(fmt, rgb):
    """§6.1 YCoCg-R, chroma offset by 1 << bpc."""
    r, g, b = rgb
    co = r - b
    t = b + (co >> 1)
    cg = g - t
    return (t + (cg >> 1), co + (1 << fmt.bpc), cg + (1 << fmt.bpc))


def lossless_units(fmt, rgb):
    """QP 0 syntax for one slice of RGB rows: MMAP, or MPP where a residual
    needs the full sample width (§6.4.4.2); P-mode after P-mode throughout.
    Returns the units and the number of MPP units."""
    height, width = len(rgb), len(rgb[0])
    samples = [[forward(fmt, px) for px in row] for row in rgb]
    units, previous, mpp = [], [0, 0, 0], 0
    for y in range(height):
        for x in range(0, width, 3):
            group = []
            for c, depth in enumerate(fmt.depth):
                mid = 1 << (depth - 1)
                a = samples[y][x - 1][c] if x else mid
                residual = []
                for j in range(3):
                    if x + j >= width:
                        residual.append(0)
                        continue
                    if y == 0:
                        pred = clamp(a + sum(residual), 0, (1 << depth) - 1)
                    else:
                        above = [samples[y - 1][k][c] for k in range(x, x + j + 1)]
                        corner = samples[y - 1][x - 1][c] if x else mid
                        pred = clamp(a + above[-1] - corner + sum(residual),
                                     min([a] + above), max([a] + above))
                    residual.append(samples[y][x + j][c] - pred)
                sizes = [signed_size(v) for v in residual]
                if max(sizes) >= depth:
                    residual = [samples[y][x + j][c] - mid if x + j < width else 0 for j in range(3)]
                    sizes = [depth] * 3
                    mpp += 1
                adjusted = min(previous[c], depth - 1)
                size = max(adjusted, max(sizes))
                bits = '0' * (size - adjusted) + ('1' if c == 0 or size < depth else '')
                if size:
                    bits += ''.join(format(v & ((1 << size) - 1), f'0{size}b') for v in residual)
                group.append(bits)
                previous[c] = (sizes[0] + sizes[1] + 2 * sizes[2] + 2) // 4
            units.append(group)
    return units, mpp


def pixel(bpc, x, y):
    """Source RGB of the colour fixtures, with the low bits in use. Columns
    30-32 hold a full-scale spike that forces MPP."""
    s = 1 << (bpc - 10)                                 # 1 at 10 bpc, 4 at 12 bpc
    if 30 <= x <= 32:
        top = (1 << bpc) - 1
        return (top, 0, top) if y % 2 == 0 else (0, top, 3 * s)
    return (s * (64 + 9 * x + 5 * y) + (x * 7 + y) % s,
            s * (900 - 4 * x + (x + y) % 5) + (x * y) % s,
            s * (300 + 3 * x + 2 * y + (x * 37) % 5) + (x + 2 * y) % s)


def ppm(fmt, w, h, rgb_rows):
    return f'P6\n{w} {h}\n{(1 << fmt.bpc) - 1}\n'.encode() + b''.join(
        v.to_bytes(2, 'big') for row in rgb_rows for px in row for v in px)


def write(name, p, payload, expected, units, info):
    (OUT / f'{name}.pps').write_bytes(p)
    (OUT / f'{name}.bin').write_bytes(payload)
    (OUT / f'{name}.expected.ppm').write_bytes(expected)
    (OUT / f'{name}.syntax.txt').write_text(''.join(' '.join(u) + '\n' for u in units))
    info.update(pps_sha256=hashlib.sha256(p).hexdigest(),
                payload_sha256=hashlib.sha256(payload).hexdigest(),
                expected_sha256=hashlib.sha256(expected).hexdigest())
    return info


def color_fixture(fmt):
    name, width, height, sw, sh = f'hbd{fmt.bpc}_color', 99, 5, 50, 3
    p = pps(fmt, width, height, sw, sh)
    chunk = int.from_bytes(p[14:16], 'big')
    payloads, units_all, details = {}, [], []
    for sy in range(2):
        for sx in range(2):
            rows = [[pixel(fmt.bpc, min(sx * sw + x, width - 1), min(sy * sh + y, height - 1))
                     for x in range(sw)] for y in range(sh)]
            units, mpp = lossless_units(fmt, rows)
            payloads[sy, sx], used = multiplex(units, fmt, chunk * sh)
            units_all += units
            details.append(dict(slice=2 * sy + sx, mux_bits=used, mpp_units=mpp))
    framed = b''.join(payloads[sy, sx][y * chunk:(y + 1) * chunk]
                      for sy in range(2) for y in range(sh) for sx in range(2))
    expected = ppm(fmt, width, height,
                   [[pixel(fmt.bpc, x, y) for x in range(width)] for y in range(height)])
    return name, write(name, p, framed, expected, units_all,
                       dict(width=width, height=height, slices=4, details=details))


def ich_fixture(fmt):
    """One P group seeds the history; the second group escapes to ICH with
    bpc + 1 - predictedSizeY zeros (Table 6-1); every later group continues
    with "1" and references index 0 in all three substreams."""
    name, width, height = f'hbd{fmt.bpc}_ich', 48, 3
    s = 1 << (fmt.bpc - 10)
    color = (600 * s + 3, 520 * s + 1, 400 * s + 2)
    y, co, cg = forward(fmt, color)
    groups = width // 3 * height
    units = []
    # Group 0: first line, first group, QP 0. MMAP predicts the midpoint for
    # the first pixel and a + R0 (+ R1) for the others, so residuals
    # (v - midpoint, 0, 0) reconstruct v three times.
    first, sizes = [], []
    for c, (v, depth) in enumerate(zip((y, co, cg), fmt.depth)):
        r = [v - (1 << (depth - 1)), 0, 0]
        size = signed_size(r[0])
        assert size < depth, 'the colour must not need MPP'
        bits = '0' * size + '1'
        first.append(bits + ''.join(format(t & ((1 << size) - 1), f'0{size}b') for t in r)
                     if size else bits)
        sizes.append(size)
    units.append(first)
    predicted = (sizes[0] + 2) // 4
    units.append(['0' * (fmt.bpc + 1 - predicted) + '00000', '00000', '00000'])
    units += [['1' + '00000', '00000', '00000'] for _ in range(groups - 2)]
    p = pps(fmt, width, height, width, height)
    chunk = int.from_bytes(p[14:16], 'big')
    payload, used = multiplex(units, fmt, chunk * height)
    return name, dict(units=units, p=p, payload=payload, used=used, color=color,
                      width=width, height=height, ycocg=(y, co, cg))


def qp_fixture(fmt, qp):
    """MPP groups with residuals Y +1, Co -1, Cg 0. Groups 0 and 1 decode at
    QP 0, the rest at the pinned QP (research/hbd-worked-note.md)."""
    name, width = f'hbd{fmt.bpc}_qp', 48
    y_level = (qp - 1) // 2 - (1 if qp + 2 == fmt.max_qp else 0)
    c_level = qp // 2 + 1 + (1 if qp + 2 == fmt.max_qp else 0)
    units, previous = [], [0, 0, 0]
    for g in range(width // 3):
        levels = (0, 0, 0) if g < 2 else (y_level, c_level, c_level)
        unit = []
        for c, (depth, level, res) in enumerate(zip(fmt.depth, levels, (1, -1, 0))):
            size = depth - level
            if g == 0:
                adjusted = 0
            else:
                adjusted = clamp(previous[c] - (level - previous_level[c]), 0, size - 1)
            bits = '0' * (size - adjusted) + ('1' if c == 0 else '')
            unit.append(bits + format(res & ((1 << size) - 1), f'0{size}b') * 3)
            previous[c] = size
        previous_level = levels
        units.append(unit)
    p = pps(fmt, width, 1, width, 1, qp)
    chunk = int.from_bytes(p[14:16], 'big')
    payload, used = multiplex(units, fmt, chunk)
    return name, dict(units=units, p=p, payload=payload, used=used, levels=(y_level, c_level))


def main():
    OUT.mkdir(exist_ok=True)
    manifest = {}
    for bpc, qp, qp_rows in ((10, 13, None), (12, 21, None)):
        fmt = Format(bpc)
        name, info = color_fixture(fmt)
        manifest[name] = info
        # ICH: every pixel is the colour of group 0.
        name, f = ich_fixture(fmt)
        rows = [[f['color']] * f['width'] for _ in range(f['height'])]
        manifest[name] = write(name, f['p'], f['payload'], ppm(fmt, f['width'], f['height'], rows),
                               f['units'], dict(width=f['width'], height=f['height'],
                                                mux_bits=f['used'], ycocg=f['ycocg']))
        # Pinned QP: gray values from the worked note.
        name, f = qp_fixture(fmt, qp)
        rgb = qp_expected(fmt, f['levels'])
        manifest[name] = write(name, f['p'], f['payload'], ppm(fmt, 48, 1, [rgb]), f['units'],
                               dict(width=48, height=1, qp=qp, qlevels=f['levels'],
                                    mux_bits=f['used']))
    (OUT / 'hbd_manifest.json').write_text(json.dumps(manifest, indent=1) + '\n')
    print(json.dumps(manifest, indent=1))


def qp_expected(fmt, levels):
    """§6.4.3 MPP and §7.7, group by group, as in the worked note."""
    y_level, c_level = levels
    ymid, cmid = 1 << (fmt.bpc - 1), 1 << fmt.bpc
    top = (1 << fmt.bpc) - 1
    out, y, co = [], 0, 0
    for g in range(16):
        qy, qc = (0, 0) if g < 2 else (y_level, c_level)
        y = clamp(ymid + (y & ((1 << qy) - 1)) + (1 << qy), 0, top)
        co = clamp(cmid + (co & ((1 << qc) - 1)) - (1 << qc), 0, 2 * cmid - 1)
        cg = cmid + (cg_prev & ((1 << qc) - 1)) if g else cmid
        cg_prev = cg
        csc_co, csc_cg = co - cmid, cg - cmid
        t = y - (csc_cg >> 1)
        b = t - (csc_co >> 1)
        out += [(clamp(csc_co + b, 0, top), clamp(csc_cg + t, 0, top), clamp(b, 0, top))] * 3
    return out


if __name__ == '__main__':
    main()
