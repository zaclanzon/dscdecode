#!/usr/bin/env python3
"""Lossless YCbCr fixtures for the .yuv output of dscdecode.

One small picture per coding format, every range pinned to QP 0, so that the
decoded samples are the chosen ones: the expected output is written from the
chosen samples in the .yuv layout (planar samples as they are, UYVY samples
in the top bits above 8 bpc, simple 4:2:2 keeping the even-position chroma),
and the Python model (pydsc.py) must decode the payload to the same bytes.
The payloads are built with pydsc's builder from residuals that reach the
chosen samples under MMAP (make_native_discriminators.mmap_residuals).

  ycc444_v11_8    YCbCr 4:4:4, DSC 1.1, 8 bpc, 12x2
  ycc444_10       YCbCr 4:4:4, DSC 1.2, 10 bpc, 12x2
  simple422_12    simple 4:2:2 (4:4:4 coding), DSC 1.2, 12 bpc, 12x2
  native422_10    native 4:2:2, DSC 1.2, 10 bpc, 24x2
  native420_14    native 4:2:0, DSC 1.2, 14 bpc, 24x4
  native420_16    native 4:2:0, DSC 1.2, 16 bpc, 24x2
"""
from pathlib import Path

from pydsc import PPS, Builder, Decoder
from make_native_discriminators import mmap_residuals

OUT = Path(__file__).parent / 'fixtures'


def sample(bpc, plane, x, y):
    """A deterministic sample spread over the range, in small steps (the
    residuals must stay below the MPP size at QP 0)."""
    mid = 1 << (bpc - 1)
    # At 16 bpc a luma prefix at QP 0 has at most 13 bits (OQ-21): residuals
    # stay within 12 bits.
    step = 1 << (min(bpc, 15) - 7)
    return mid + step * (((3 * x + 5 * y + 7 * plane) % 9) - 4) + (x * 13 + y * 29 + plane) % step


def enc(values, bpc, top):
    if bpc <= 8:
        return bytes(values)
    return b''.join((v << (16 - bpc) if top else v).to_bytes(2, 'little') for v in values)


def build(name, version, bpc, width, height, fmt):
    rgb0 = dict(rgb=0, simple_422=int(fmt == 'simple422'), native_422=int(fmt == 'native422'),
                native_420=int(fmt == 'native420'))
    pps = PPS(version=version, bpc=bpc, line_buf=bpc + 1 if bpc < 16 else 16, width=width, height=height,
              bpp16=1023 if fmt.startswith('native') else 768, ranges=((0, 0, 0),) * 15,
              flat_min=31 if bpc == 16 else 15, flat_max=31 if bpc == 16 else 15,
              limit0=31 if bpc == 16 else 15, limit1=31 if bpc == 16 else 15, **rgb0)
    b = Builder(pps)
    cw = pps.coded_width
    # The samples of the coded picture: container units for native modes.
    Y = [[sample(bpc, 0, x, y) for x in range(width)] for y in range(height)]
    # Simple 4:2:2 codes chroma at every position and outputs the even ones.
    C = [[[sample(bpc, k, x, y) for x in range(width)] for y in range(height)] for k in (1, 2)]
    for y in range(height):
        for x in range(0, cw, 3):
            n = min(3, cw - x)
            xs = range(x, x + 3)
            if fmt == 'native422':
                units = [[Y[y][2 * k] for k in xs], [C[0][y][2 * k] for k in xs],
                         [C[1][y][2 * k] for k in xs], [Y[y][2 * k + 1] for k in xs]]
            elif fmt == 'native420':
                units = [[Y[y][2 * k] for k in xs], [Y[y][2 * k + 1] for k in xs],
                         [C[y % 2][y][2 * k] for k in xs]]
            else:
                units = [[Y[y][k] for k in xs], [C[0][y][k] for k in xs], [C[1][y][k] for k in xs]]
            units = [u[:n] + [u[n - 1]] * (3 - n) for u in units]
            res = mmap_residuals(b, x, y, units)
            for u in res:
                u[n:] = [0] * (3 - n)
            b.group(res=res)
    payload, _ = b.payload()
    # Expected output, from the chosen samples.
    if fmt == 'native420':
        cb = [C[0][y][2 * k] for y in range(0, height, 2) for k in range(width // 2)]
        cr = [C[1][y][2 * k] for y in range(1, height, 2) for k in range(width // 2)]
        expected = enc([v for r in Y for v in r], bpc, False) + enc(cb, bpc, False) + enc(cr, bpc, False)
    elif fmt in ('native422', 'simple422'):
        expected = enc([v for y in range(height) for k in range(width // 2)
                        for v in (C[0][y][2 * k], Y[y][2 * k], C[1][y][2 * k], Y[y][2 * k + 1])], bpc, True)
    else:
        expected = b''.join(enc([v for r in plane for v in r], bpc, False) for plane in (Y, C[0], C[1]))
    decoded = Decoder(pps).decode(payload).output()
    assert decoded == expected, (name, 'pydsc does not reproduce the chosen samples')
    (OUT / f'{name}.pps').write_bytes(pps.bytes())
    (OUT / f'{name}.bin').write_bytes(payload)
    (OUT / f'{name}.expected.yuv').write_bytes(expected)
    return name


def main():
    for args in (('ycc444_v11_8', 1, 8, 12, 2, 'ycc444'), ('ycc444_10', 2, 10, 12, 2, 'ycc444'),
                 ('simple422_12', 2, 12, 12, 2, 'simple422'), ('native422_10', 2, 10, 24, 2, 'native422'),
                 ('native420_14', 2, 14, 24, 4, 'native420'), ('native420_16', 2, 16, 24, 2, 'native420')):
        print(build(*args))


if __name__ == '__main__':
    main()
