#!/usr/bin/env python3
"""Block-prediction fixtures and the BP left-boundary discriminator.

Fixtures (tests/fixtures/bp_*): lossless at QP 0, so the expected output is
the source image itself, given by the formulas in research/bp-worked-note.md.
The bitstream carries the residuals for the predictor this constructor
selects in each group. A decoder that selects differently reconstructs
different pixels.

Discriminator (tests/discriminators/oq4_bp_left): a picture whose BP
decisions depend on how samples left of the slice are read (OQ-4). The
entropy parse does not depend on the predictor, so both readings decode the
same bits into different pixels.

Everything here was written from DSC 1.1 §§6.1, 6.4.1-6.4.6, 6.6.1 and the
DSC 1.2b §6.4.4.1 correction, separately from the decoder. It supports QP 0
only, with every RC range pinned to QP 0, no ICH and no flatness.
"""
from itertools import product
from pathlib import Path
import hashlib
import json

from make_vectors import multiplex

ROOT = Path(__file__).parent
DEPTH = (8, 9, 9)              # Y, Co, Cg bits at 8 bpc (§6.1)
MID = (128, 256, 256)
CANDIDATES = (-1, -3, -4, -5, -6, -7, -8, -9, -10)   # §6.4.4.1, tie order
BP_READINGS = {
    'bp_left': ('replicate', 'midpoint'),     # OQ-4
    'bp_edge': ('window', 'before'),          # OQ-13
    'bp_sad': ('shift', 'clip'),              # OQ-10
}


def signed_size(v):
    if v == 0:
        return 0
    w = 1
    while not -(1 << (w - 1)) <= v < (1 << (w - 1)):
        w += 1
    return w


def clamp(v, lo, hi):
    return max(lo, min(hi, v))


def to_ycocg(r, g, b):
    """§6.1 forward YCoCg-R, chroma offset to unsigned."""
    co = r - b
    t = b + (co >> 1)
    cg = g - t
    return (t + (cg >> 1), co + 256, cg + 256)


def to_rgb(y, co, cg):
    co, cg = co - 256, cg - 256
    t = y - (cg >> 1)
    b = t - (co >> 1)
    return tuple(clamp(v, 0, 255) for v in (co + b, cg + t, b))


# --- §6.4.4.1 BP search on the previous line --------------------------------

def prev_sample(prev, x, c, r):
    if x < 0:     # OQ-4: left of the slice
        return MID[c] if r['bp_left'] == 'midpoint' else prev[0][c]
    return prev[min(x, len(prev) - 1)][c]


def bp_search(prev, h, r):
    best = vector = None
    for v in CANDIDATES:
        total = 0
        for b in range(3):
            part = 0
            for j in range(3):
                pos = h - 6 + 3 * b + j
                for c in range(3):
                    d = abs(prev_sample(prev, pos, c, r) - prev_sample(prev, pos + v, c, r))
                    part += min(d >> (DEPTH[c] - 7), 63)
            total += min(part, 511)
        sad = total >> 3 if r['bp_sad'] == 'shift' else min(total, 511)
        if best is None or sad < best:
            best, vector = sad, v
    return vector, best


def recent_edge(prev, last, r):
    """lastEdgeCount < 3: an edge at one of the three samples ending at last."""
    return any(abs(prev_sample(prev, q, c, r) - prev_sample(prev, q - 1, c, r)) > 32
               for q in (last - 2, last - 1, last) for c in range(3))


# --- one slice at QP 0 ------------------------------------------------------

def encode_slice(img, r, residual_source=None):
    """Lossless QP-0 syntax for one slice under readings r.

    img: rows of (Y, Co, Cg). Returns (units, recon, log). If residual_source
    is given, its residuals are coded instead (used by the discriminator to
    decode fixed bits under another reading); recon then follows from them.
    """
    width = len(img[0])
    predicted = [0, 0, 0]
    units, log, recon = [], [], []
    prev = None
    g = 0
    for y, row in enumerate(img):
        line, count = [], 0
        for h in range(0, width, 3):
            n = min(3, width - h)
            use_bp, vector = False, None
            if y and prev is not None:
                vector, sad = bp_search(prev, h, r)
                count = 0 if vector == -1 else count + (h >= 9)
                last = h + 2 if r['bp_edge'] == 'window' else h - 1
                use_bp = vector != -1 and count >= 3 and n == 3 and recent_edge(prev, last, r)
            entry = dict(line=y, hpos=h, vector=vector, count=count, bp=use_bp, mpp=[])
            group_units, group_res = [], []
            for c in range(3):
                target = [row[h + j][c] if j < n else None for j in range(3)]
                res = None if residual_source is None else residual_source[g][c]
                res, pred, mpp = predict_component(line, prev, h, y, c, n, target, use_bp,
                                                   vector, res)
                bits, need = syntax(res, predicted[c], DEPTH[c], mpp, c)
                predicted[c] = (need[0] + need[1] + 2 * need[2] + 2) >> 2
                group_units.append(bits)
                group_res.append(res)
                entry['mpp'].append(mpp)
                for j in range(n):
                    if len(line) <= h + j:
                        line.append([0, 0, 0])
                    line[h + j][c] = clamp(pred[j] + res[j], 0, (1 << DEPTH[c]) - 1)
            units.append(group_units)
            entry['residuals'] = list(zip(group_res, entry['mpp']))
            log.append(entry)
            g += 1
        recon.append([tuple(p) for p in line])
        prev = recon[-1]
    return units, recon, log


def predict_component(line, prev, h, y, c, n, target, use_bp, vector, fixed):
    """Predictions and residuals for one component of one group (§6.4)."""
    top = (1 << DEPTH[c]) - 1
    a = line[h - 1][c] if h else MID[c]

    def predictions(res):
        out = []
        for j in range(3):
            if use_bp:
                out.append(line[h + j + vector][c] if j < n else 0)
            elif not y:
                out.append(clamp(a + sum(res[:j]), 0, top) if j else a)
            else:
                ref = [prev[min(h + k, len(prev) - 1)][c] for k in range(j + 1)]
                left = prev[h - 1][c] if h else MID[c]
                out.append(clamp(a + ref[-1] - left + sum(res[:j]),
                                 min([a] + ref), max([a] + ref)))
        return out

    if fixed is not None:
        mpp = fixed[1]
        res = list(fixed[0])
        pred = [MID[c]] * 3 if mpp else predictions(res)
        return res, pred, mpp
    res = []
    for j in range(3):   # lossless: each residual is exact given the earlier ones
        if j >= n:
            res.append(0)
            continue
        p = predictions(res + [0, 0, 0])[j]
        res.append(target[j] - p)
    pred = predictions(res)
    if max(signed_size(v) for v in res) >= DEPTH[c]:      # §6.4.4.2: MPP
        res = [target[j] - MID[c] if j < n else 0 for j in range(3)]
        return res, [MID[c]] * 3, True
    return res, pred, False


def syntax(res, predicted, depth, mpp, c):
    """§6.6.1 DSU-VLC unit for P-mode after a P-mode group, QP 0."""
    pred = min(predicted, depth - 1)
    need = [depth] * 3 if mpp else [signed_size(v) for v in res]
    width = depth if mpp else max(pred, max(need))
    bits = '0' * (width - pred)
    if c == 0 or width < depth:
        bits += '1'
    bits += ''.join(format(v & ((1 << width) - 1), f'0{width}b') for v in res) if width else ''
    return bits, need


# --- pictures and PPS -------------------------------------------------------

def pps(pic_w, pic_h, slice_w, slice_h, bpp16=384):
    p = bytearray(128)

    def word(i, v):
        p[i:i + 2] = v.to_bytes(2, 'big')
    chunk = (slice_w * bpp16 + 127) // 128
    p[0], p[3] = 0x11, 0x89
    p[4] = 0x30 | (bpp16 >> 8)          # block_pred_enable, convert_rgb
    p[5] = bpp16 & 255
    for i, v in [(6, pic_h), (8, pic_w), (10, slice_h), (12, slice_w), (14, chunk),
                 # Delay longer than any slice here: no bits leave the buffer.
                 # (1023 + 512) x 24 bpp keeps rc_bits within 16 bits.
                 (16, 1023), (18, 512), (32, 6144), (34, 256), (38, 8192)]:
        word(i, v)
    p[21] = 8
    p[36] = p[37] = 15                 # no flatness syntax at QP 0
    p[40], p[41], p[42], p[43] = 6, 11, 11, 0x33
    p[44:58] = bytes((14, 28, 42, 56, 70, 84, 98, 105, 112, 119, 121, 123, 125, 126))
    # All fifteen ranges min = max = 0: QP 0 throughout, lossless.
    return bytes(p), chunk


DEFAULT = dict(bp_left='replicate', bp_edge='window', bp_sad='shift')


def picture(pixel, pic_w, pic_h, slice_w, slice_h, r=DEFAULT):
    """Encode a picture. Returns (pps, framed payload, per-slice logs, units)."""
    header, chunk = pps(pic_w, pic_h, slice_w, slice_h)
    nx, ny = -(-pic_w // slice_w), -(-pic_h // slice_h)
    payloads, logs, all_units = {}, {}, {}
    for sy, sx in product(range(ny), range(nx)):
        img = [[to_ycocg(*pixel(min(sx * slice_w + x, pic_w - 1), min(sy * slice_h + y, pic_h - 1)))
                for x in range(slice_w)] for y in range(slice_h)]
        units, recon, log = encode_slice(img, r)
        assert recon == [[tuple(p) for p in row] for row in img], 'not lossless'
        payload, _, _ = multiplex(units, chunk * slice_h)
        payloads[sy, sx], logs[sy, sx], all_units[sy, sx] = payload, log, units
    framed = b''.join(payloads[sy, sx][y * chunk:(y + 1) * chunk]
                      for sy in range(ny) for y in range(slice_h) for sx in range(nx))
    return header, framed, logs, all_units


def expected_ppm(pixel, w, h):
    return f'P6\n{w} {h}\n255\n'.encode() + bytes(v for y in range(h) for x in range(w)
                                                  for v in pixel(x, y))


# Period-3 colour patterns. Every neighbouring pair differs by more than 32
# in some component, so the edge condition holds everywhere.
PATTERNS = [
    ((200, 40, 40), (40, 200, 40), (40, 40, 200)),
    ((220, 220, 60), (60, 220, 220), (220, 60, 220)),
    ((250, 130, 10), (10, 250, 130), (130, 10, 250)),
]


def bp_left_edge(x, y):
    return PATTERNS[y][x % 3]


def bp_slice_boundary(x, y):
    """Two 35-wide slices. Slice 1 is the other two patterns, shifted."""
    if x < 35:
        return PATTERNS[y][x % 3]
    return PATTERNS[(y + 1) % 3][(x - 35 + 1) % 3]


def fixtures(manifest):
    out = ROOT / 'fixtures'
    for name, pixel, pic_w, pic_h, slice_w, slice_h in [
            ('bp_left_edge', bp_left_edge, 36, 3, 36, 3),
            ('bp_slice_boundary', bp_slice_boundary, 70, 3, 35, 3)]:
        results = {}
        for values in product(*BP_READINGS.values()):
            r = dict(zip(BP_READINGS, values))
            header, framed, logs, _ = picture(pixel, pic_w, pic_h, slice_w, slice_h, r)
            results[values] = (header, framed, logs)
        base = results[tuple(v[0] for v in BP_READINGS.values())]
        # Robust fixture: every BP reading makes the same decisions.
        assert all(v[1] == base[1] for v in results.values()), name
        header, framed, logs = base
        (out / f'{name}.pps').write_bytes(header)
        (out / f'{name}.bin').write_bytes(framed)
        ppm = expected_ppm(pixel, pic_w, pic_h)
        (out / f'{name}.expected.ppm').write_bytes(ppm)
        decisions = {f'slice{sx}': [[e['hpos'], e['vector'], e['count'], e['bp']]
                                    for e in log if e['line'] == 1]
                     for (sy, sx), log in logs.items()}
        manifest[name] = dict(width=pic_w, height=pic_h, slice_width=slice_w,
                              line1_decisions=decisions,
                              bp_groups=sum(e['bp'] for log in logs.values() for e in log),
                              payload_sha256=hashlib.sha256(framed).hexdigest(),
                              expected_sha256=hashlib.sha256(ppm).hexdigest())


# --- OQ-4 discriminator -----------------------------------------------------

def decode_all(img, source_log):
    """Decode fixed residuals under every BP reading; RGB rows per reading."""
    source = [e['residuals'] for e in source_log]
    out = {}
    for values in product(*BP_READINGS.values()):
        r = dict(zip(BP_READINGS, values))
        units, recon, log = encode_slice(img, r, residual_source=source)
        out[values] = (units, [[to_rgb(*p) for p in row] for row in recon], log)
    return out


def left_boundary_line0(seed):
    """Gray first line, deterministic in seed (xorshift32)."""
    state = seed
    values = []
    for _ in range(30):
        state ^= (state << 13) & 0xffffffff
        state ^= state >> 17
        state ^= (state << 5) & 0xffffffff
        values.append(state % 256)
    return values


def oq4_candidate(seed):
    line0 = left_boundary_line0(seed)
    line1 = [(40 + 7 * x) % 256 for x in range(30)]
    img = [[(v, 256, 256) for v in line0], [(v, 256, 256) for v in line1]]
    _, _, log = encode_slice(img, DEFAULT)
    return img, log, decode_all(img, log)


def oq4_ok(results):
    by_left = {}
    reference_units = next(iter(results.values()))[0]
    for values, (units, rgb, _) in results.items():
        if units != reference_units:
            return False
        by_left.setdefault(values[0], set()).add(repr(rgb))
    return all(len(s) == 1 for s in by_left.values()) and len({next(iter(s)) for s in by_left.values()}) == 2


OQ4_SEED = 17   # the first seed in 1..399 with the property oq4_ok() checks


def oq4_bp_left(out_dir):
    """Write the OQ-4 discriminator; return its manifest entry."""
    img, log, results = oq4_candidate(OQ4_SEED)
    assert oq4_ok(results), 'OQ-4 property lost'
    header, chunk = pps(30, 2, 30, 2)
    units = results[tuple(v[0] for v in BP_READINGS.values())][0]
    payload, used, _ = multiplex(units, chunk * 2)
    (out_dir / 'oq4_bp_left.pps').write_bytes(header)
    (out_dir / 'oq4_bp_left.bin').write_bytes(payload)
    (out_dir / 'oq4_bp_left.syntax.txt').write_text(''.join(' '.join(u) + '\n' for u in units))
    entry = dict(question='bp_left', vary=list(BP_READINGS), assumes={}, width=30, height=2,
                 mux_bits=used, payload_bits=8 * chunk * 2, seed=OQ4_SEED,
                 line0_luma=[p[0] for p in img[0]],
                 pps_sha256=hashlib.sha256(header).hexdigest(),
                 payload_sha256=hashlib.sha256(payload).hexdigest(), readings={})
    for left in BP_READINGS['bp_left']:
        key = (left,) + tuple(v[0] for v in list(BP_READINGS.values())[1:])
        _, rgb, dlog = results[key]
        ppm = b'P6\n30 2\n255\n' + bytes(v for row in rgb for px in row for v in px)
        (out_dir / f'oq4_bp_left.{left}.expected.ppm').write_bytes(ppm)
        entry['readings'][left] = dict(
            expected_sha256=hashlib.sha256(ppm).hexdigest(),
            line1_decisions=[[e['hpos'], e['vector'], e['count'], e['bp']]
                             for e in dlog if e['line'] == 1])
    return entry


def main():
    manifest = {}
    fixtures(manifest)
    (ROOT / 'fixtures' / 'bp_manifest.json').write_text(json.dumps(manifest, indent=1) + '\n')
    print(json.dumps(manifest, indent=1))


if __name__ == '__main__':
    main()
