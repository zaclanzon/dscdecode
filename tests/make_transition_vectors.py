#!/usr/bin/env python3
"""Hand-scheduled QP/flatness fixtures; no encoder or decoder oracle.

See VECTORS.md for arithmetic and limitations of the timing interpretation.
The QP sequence and pixels below are fixed expectations, not RC simulation.
"""
from pathlib import Path
from make_vectors import multiplex, pps

OUT = Path(__file__).parent / 'fixtures'


def parameters(width=96, depth=9):
    p = bytearray(128)
    p[0], p[3], p[4], p[5] = 0x11, 0x80 | depth, 0x11, 0x80
    for offset, value in [(6, 1), (8, width), (10, 1), (12, width),
                          (14, width * 3), (16, 512), (18, 512),
                          (32, 2048), (34, 4096), (38, 8192)]:
        p[offset:offset+2] = value.to_bytes(2, 'big')
    p[21], p[36], p[37] = 8, 3, 12
    p[40], p[41], p[42], p[43] = 6, 11, 11, 0x33
    p[44:58] = bytes(range(8, 120, 8))
    for i in range(15):
        p[58+2*i:60+2*i] = ((8 << 11) | ((15 if i == 14 else 8) << 6)).to_bytes(2, 'big')
    return p


def write(name, p, units, pixels):
    payload, _, _ = multiplex(units, int.from_bytes(p[14:16], 'big'))
    width = int.from_bytes(p[12:14], 'big')
    (OUT / f'{name}.pps').write_bytes(p)
    (OUT / f'{name}.bin').write_bytes(payload)
    (OUT / f'{name}.syntax.txt').write_text('\n'.join(' '.join(u) for u in units) + '\n')
    (OUT / f'{name}.expected.ppm').write_bytes(f'P6\n{width} 1\n255\n'.encode() + bytes(pixels))


def main():
    # MPP widths at QP 0: (8,9,9); QP 8: (5,4,4);
    # QP 4: (7,6,6); QP 1: (8,8,8), DSC 1.1 Table 6-3.
    widths = {0: (8,9,9), 8: (5,4,4), 4: (7,6,6), 1: (8,8,8)}
    for flat in (False, True):
        qps = [0,0] + [8]*30
        grays = [129,129] + [137]*30
        if flat:
            qps[5], qps[9] = 4, 1
            grays[5:10] = [131,139,139,139,129]
        units = []
        for g, qp in enumerate(qps):
            unit = []
            for c, w in enumerate(widths[qp]):
                # Previous unit was MPP, so adjusted size is maxSize-1,
                # including when the quantization level changes.
                prefix = '0' * (w if g == 0 else 1) + ('1' if c == 0 else '')
                residual = 1 if c == 0 else 0
                unit.append(prefix + format(residual, f'0{w}b') * 3)
            if g % 4 == 3:
                unit[0] = ('1' if flat and g in (3,7) else '0') + unit[0]
            if flat and g in (4,8):
                unit[0] = ('000' if g == 4 else '100') + unit[0]
            units.append(unit)
        write('qp_flatness' if flat else 'qp_transition', parameters(), units,
              [gray for gray in grays for _ in range(9)])

    # A width-95 slice ends every line with two real samples. Syntax must
    # still contain three residuals/indices, with canonical padding (§6.6).
    for ich in (False, True):
        units = [['1','1','1'] for _ in range(96)]
        if ich:
            for g in range(1, 96):
                units[g] = [('0'*9 if g == 1 else '1')+'00000', '00000', '00000']
            units[-1][2] = '00001'  # Must repeat the second index (zero).
        else:
            units[-1][0] = '01001'  # Width 1, residuals 0,0,+/-1; last is padding.
        name = 'invalid_partial_ich' if ich else 'invalid_partial_residual'
        payload, _, _ = multiplex(units, 95*3)
        (OUT/f'{name}.pps').write_bytes(pps(95,3,95))
        (OUT/f'{name}.bin').write_bytes(payload)


if __name__ == '__main__':
    main()
