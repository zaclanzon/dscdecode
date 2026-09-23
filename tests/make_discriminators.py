#!/usr/bin/env python3
"""Discriminator inputs for open questions OQ-1, OQ-2 and OQ-3.

Each discriminator is a PPS plus a one-line, one-slice payload. Its entropy
parse is identical under every reading of every rate-control question this
repository switches on (RESEARCH.md: OQ-1, OQ-2, OQ-3, OQ-12). Its decoded
pixels depend on the reading of exactly one question and differ between that
question's two readings. The divergence is confined to the last group of the
slice, so no later syntax depends on it.

The rate-control model here was written separately from the decoder, from DSC
1.1 §§6.4.1, 6.6.1, 6.8 and the readings in RESEARCH.md. It exists to build
these inputs and to predict their outputs. It is not a general encoder: it
supports only the first line of a slice, P-mode MMAP groups with explicit
residuals, flatness signaling, a scale of 8, and zero BPG offsets.

python3 tests/make_discriminators.py rewrites tests/discriminators/*, except
the hand-written README.md.
"""
from dataclasses import dataclass
from itertools import product
from pathlib import Path
import hashlib
import json

from make_vectors import multiplex

OUT = Path(__file__).parent / 'discriminators'

# DSC 1.1 Table 6-2, 8 bpc: masterQp -> (qLevelY, qLevelC).
QLEVEL_Y = (0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 5, 6, 7)
QLEVEL_C = (0, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 8, 8, 8)
DEPTH = (8, 9, 9)  # Y, Co, Cg sample bits for 8 bpc RGB (§6.1)

# Every switch the decoder offers for a rate-control question.
READINGS = {
    'flat_restart': ('next-cycle', 'in-flight'),
    'threshold_eq': ('lower', 'upper'),
    'frac_reset': ('chunk', 'literal'),
    'delay_offset': ('inclusive', 'exclusive'),
}


def signed_size(v):
    """Bits needed for v in two's complement; zero needs none (§6.6.1)."""
    if v == 0:
        return 0
    w = 1
    while not -(1 << (w - 1)) <= v < (1 << (w - 1)):
        w += 1
    return w


def clamp(v, lo, hi):
    return max(lo, min(hi, v))


@dataclass
class Params:
    width: int = 96
    bpp16: int = 128              # bits_per_pixel in 1/16 bit
    xmit_delay: int = 512
    dec_delay: int = 512
    initial_offset: int = 6144
    final_offset: int = 256
    model_size: int = 8192
    thresholds: tuple = (14, 28, 42, 56, 70, 84, 98, 105, 112, 119, 121, 123, 125, 126)
    ranges: tuple = ((0, 0, 0),) * 15   # (min_qp, max_qp, bpg_offset)
    flat_min: int = 15
    flat_max: int = 15
    edge_factor: int = 6
    limit0: int = 15
    limit1: int = 15
    tgt_hi: int = 3
    tgt_lo: int = 3

    @property
    def chunk(self):
        return (self.width * self.bpp16 + 127) // 128

    def pps(self):
        p = bytearray(128)

        def word(i, v):
            p[i:i + 2] = v.to_bytes(2, 'big')
        p[0] = 0x11                     # DSC 1.1
        p[3] = 0x89                     # 8 bpc, 9-bit line buffer
        p[4] = 0x10 | (self.bpp16 >> 8) # convert_rgb; BP, VBR, 4:2:2 off
        p[5] = self.bpp16 & 0xff
        for i, v in [(6, 1), (8, self.width), (10, 1), (12, self.width),
                     (14, self.chunk), (16, self.xmit_delay), (18, self.dec_delay),
                     (32, self.initial_offset), (34, self.final_offset),
                     (38, self.model_size)]:
            word(i, v)
        p[21] = 8                       # initial_scale_value: unity
        p[36], p[37] = self.flat_min, self.flat_max
        p[40], p[41], p[42] = self.edge_factor, self.limit0, self.limit1
        p[43] = (self.tgt_hi << 4) | self.tgt_lo
        p[44:58] = bytes(self.thresholds)
        for i, (lo, hi, off) in enumerate(self.ranges):
            word(58 + 2 * i, (lo << 11) | (hi << 6) | (off & 63))
        return bytes(p)


@dataclass
class Group:
    res: tuple                        # quantized residuals (Y, Co, Cg), 3 each
    width: tuple = (None, None, None) # suffix size; None: max(predicted, needed)
    flat_flag: int = 0                # sent only when grpNum % 4 == 3
    flat_type: int = 0                # sent after a set flag, when QP >= 7
    flat_pos: int = 0


class Model:
    """First-line decoding state under one combination of readings."""

    def __init__(self, p, readings):
        self.p, self.r = p, readings
        self.fullness = 0
        self.offset_q11 = (p.initial_offset - p.model_size) * 2048
        self.clamped = False
        self.pixels = 0
        self.frac = self.chunk_bits = self.chunk_pixels = 0
        self.qp = self.queued = self.prev = self.prev2 = self.used = 0
        self.prev_ideal = 0
        self.saved = None
        self.predicted, self.last_level = [0, 0, 0], [0, 0, 0]
        self.flag = 0
        self.flat_group, self.flat_type = None, 0
        self.recon = []
        self.log = []

    # --- §6.8.4 short-term RC, Figures 6-12 and 6-13 -------------------
    def short_term(self, inp, prev, prev2):
        p = self.p
        lo, hi = inp['target'] - p.tgt_lo, inp['target'] + p.tgt_hi
        if inp['model'] > -172:
            return p.ranges[14][1]
        if inp['ideal'] == 3:
            return max(prev - 1, 0, inp['min'] // 2)
        if inp['actual'] < lo and inp['ideal'] < lo:
            return max(prev - 1, 0, inp['min'])
        if inp['actual'] > hi and inp['fullness'] >= 64:
            cur = max(inp['min'], prev)
            edge = inp['ideal'] * 2 < inp['prev_ideal'] * p.edge_factor
            if cur == prev2:
                permit = edge
            elif cur < prev2:
                permit = edge and cur < p.limit0
            else:
                permit = cur < p.limit1
            return min(inp['max'], cur + ((inp['actual'] - inp['target']) >> 1)) if permit else cur
        return prev

    # --- §6.8.5.2 flatness override with the OQ-1 readings -------------
    def flatness(self):
        q = self.qp
        if q == self.p.ranges[14][1]:
            return
        master = 1 if (self.flat_type and q >= 7) else max(q - 4, 0)
        if master == q:
            return
        self.qp = master
        redo = self.short_term(self.saved, master, self.used)
        if self.r['flat_restart'] == 'in-flight':
            self.queued, self.prev2, self.prev = redo, master, redo
        else:
            self.prev = master

    # --- §6.8.1 bit removal for one pixel, OQ-3 readings ----------------
    def drain(self):
        p = self.p
        if self.pixels < p.xmit_delay:
            return 0
        literal = self.r['frac_reset'] == 'literal'
        self.frac += p.bpp16
        removed, self.frac = self.frac >> 4, self.frac & 15
        self.chunk_bits += removed
        if literal and (self.pixels - p.xmit_delay) % p.width == 0:
            self.frac = 0
        self.chunk_pixels += 1
        if self.chunk_pixels == p.width:
            pad = 8 * p.chunk - self.chunk_bits
            assert 0 <= pad <= (9 if literal else 8)
            removed += pad
            self.chunk_bits = self.chunk_pixels = 0
            if not literal:
                self.frac = 0
        return removed

    def rc_step(self, g, n, actual, ideal):
        p = self.p
        self.fullness += actual
        assert self.fullness <= (p.xmit_delay + p.dec_delay) * p.bpp16 // 16
        before = self.pixels
        for _ in range(n):
            self.pixels += 1
            self.fullness -= self.drain()
        assert self.fullness >= 0, ('buffer underflow', g)
        end = p.xmit_delay - (1 if self.r['delay_offset'] == 'exclusive' and p.xmit_delay else 0)
        delayed = min(n, max(0, end - before))
        self.offset_q11 -= delayed * p.bpp16 * 128   # BPG offsets are all zero
        limit = (p.final_offset - p.model_size) * 2048
        self.clamped = self.clamped or self.offset_q11 < limit
        if self.clamped:
            self.offset_q11 = min(self.offset_q11, limit)
        model = self.fullness + (self.offset_q11 >> 11)   # scale 8: no change
        assert model <= 0
        bounds = [t * 64 - p.model_size for t in p.thresholds]
        if self.r['threshold_eq'] == 'upper':
            rng = sum(model >= b for b in bounds)
        else:
            rng = sum(model > b for b in bounds)
        lo, hi, off = p.ranges[rng]
        inp = dict(fullness=self.fullness, model=model, actual=actual, ideal=ideal,
                   target=(3 * p.bpp16 + 8) // 16 + off, prev_ideal=self.prev_ideal,
                   min=lo, max=hi)
        nxt = self.short_term(inp, self.prev, self.prev2)
        self.log.append(dict(group=g, qp=self.qp, actual=actual, ideal=ideal,
                             fullness=self.fullness, model=model, range=rng,
                             on_threshold=model in bounds, generated=nxt))
        self.saved, self.used = inp, self.qp
        self.prev2, self.prev, self.prev_ideal = self.prev, nxt, ideal
        self.qp, self.queued = self.queued, nxt

    # --- one group: syntax (§4.5, §6.6.1), pixels (§6.4.1, §6.4.6), RC -
    def group(self, g, spec):
        p = self.p
        if g == self.flat_group:
            self.flatness()
        qp = self.qp
        level = (QLEVEL_Y[qp], QLEVEL_C[qp], QLEVEL_C[qp])
        flat_bits = ''
        if g % 4 == 3:
            self.flag = 0
            if p.flat_min <= qp <= p.flat_max:
                flat_bits, self.flag = str(spec.flat_flag), spec.flat_flag
            else:
                assert not spec.flat_flag, ('flag not sendable', g)
        elif g % 4 == 0 and self.flag:
            self.flat_type = spec.flat_type if qp >= 7 else 0
            if qp >= 7:
                flat_bits += str(spec.flat_type)
            flat_bits += format(spec.flat_pos, '02b')
            self.flat_group = g + 1 + spec.flat_pos
        units, sizes = [], []
        for c in range(3):
            max_size = DEPTH[c] - level[c]
            pred = clamp(self.predicted[c] - (level[c] - self.last_level[c]), 0, max_size - 1)
            need = [signed_size(v) for v in spec.res[c]]
            w = spec.width[c] if spec.width[c] is not None else max(pred, max(need))
            assert max(need) <= w < max_size and w >= pred, ('size', g, c, pred, need, w, max_size)
            bits = '0' * (w - pred) + '1'
            if w:
                bits += ''.join(format(v & ((1 << w) - 1), f'0{w}b') for v in spec.res[c])
            units.append(bits)
            sizes.append(need)
            self.predicted[c] = (need[0] + need[1] + 2 * need[2] + 2) >> 2
            self.last_level[c] = level[c]
        units[0] = flat_bits + units[0]
        # First line of a slice: P0 = a, P1 = a+R0, P2 = a+R0+R1, each clamped.
        x = 3 * g
        out = []
        for c in range(3):
            top = (1 << DEPTH[c]) - 1
            a = self.recon[x - 1][c] if x else 1 << (DEPTH[c] - 1)
            r = [v << level[c] for v in spec.res[c]]
            pred = [a, clamp(a + r[0], 0, top), clamp(a + r[0] + r[1], 0, top)]
            out.append([clamp(pred[j] + r[j], 0, top) for j in range(3)])
        n = min(3, p.width - x)
        self.recon += [tuple(out[c][j] for c in range(3)) for j in range(n)]
        actual = sum(map(len, units))
        ideal = sum(1 + 3 * max(s) for s in sizes)
        self.rc_step(g, n, actual, ideal)
        return units

    def rgb(self):
        """§7.7 inverse YCoCg-R to 8-bit RGB."""
        out = bytearray()
        for y, co, cg in self.recon:
            co, cg = co - 256, cg - 256
            t = y - (cg >> 1)
            b = t - (co >> 1)
            out += bytes(clamp(v, 0, 255) for v in (co + b, cg + t, b))
        return bytes(out)


def simulate(p, groups, readings):
    m = Model(p, readings)
    units = [m.group(g, s) for g, s in enumerate(groups)]
    return m, units


def all_readings():
    names = list(READINGS)
    for values in product(*(READINGS[n] for n in names)):
        yield dict(zip(names, values))


# --- the three discriminators -------------------------------------------

def small(sign, flat_flag=0):
    """A 12-bit QP-0 group: luma size 3, chroma residuals zero."""
    y = (3, -3, 2) if sign > 0 else (-3, 3, -2)
    return Group((y, (0, 0, 0), (0, 0, 0)), flat_flag=flat_flag)


LAST = Group(((3, -3, 2), (1, -2, 1), (1, -2, 1)))  # sizes 3, 2, 2


def oq2_threshold_equality():
    """rcModelFullness descends onto threshold 5 exactly at group 29.

    Ranges 6-14 pin QP 0; ranges 0-5 pin QP 8. Every earlier group is above
    threshold 5, in ranges 6-14, under both readings. Group 29 is small, so
    the decrement branch returns MAX(prevQp-1, minQp) of its range: QP 0 in
    range 6 (equality counts as the upper range) or QP 8 in range 5
    (equality stays in the lower range). That QP decodes group 31.
    """
    p = Params(bpp16=128, xmit_delay=128,
               ranges=((8, 8, 0),) * 6 + ((0, 0, 0),) * 9)
    groups = [small(+1 if g % 2 == 0 else -1) for g in range(30)]
    groups.append(Group(((0, 0, 0),) * 3))  # predicted size 0 for group 31
    groups.append(LAST)
    # Solve initial_offset so the model fullness after group 29 is exactly
    # threshold 5 (84*64-8192 = -2816). Before the solve the QP never depends
    # on the offset, because every group stays in ranges pinned to QP 0.
    m, _ = simulate(p, groups, dict(flat_restart='next-cycle', threshold_eq='lower',
                                    frac_reset='chunk', delay_offset='inclusive'))
    p.initial_offset += (84 * 64 - p.model_size) - m.log[29]['model']
    return 'oq2_threshold_equality', 'threshold_eq', p, groups


def oq3_fractional_bpp():
    """At 8.5 bpp the literal reset drops pixel #initial_xmit_delay's half bit.

    With initial_xmit_delay 33, group 29 ends 58 pixels after the delay began,
    where the chunk reading has removed one bit more than the literal
    reading. Group 29 is large; its buffer fullness is 63 bits under the
    chunk reading and 64 under the literal one. Only at 64 does Figure 6-12
    take the increment branch, which yields MIN(maxQp 8, 0 + incrAmount) = 8.
    Otherwise the QP stays 0. Every range is (min 0, max 8), so the range,
    the threshold and the offset do not matter.
    """
    p = Params(bpp16=136, xmit_delay=33, dec_delay=64, initial_offset=4096,
               ranges=((0, 8, 0),) * 15, edge_factor=15)
    big = Group(((40, -40, 40), (1, -2, 1), (1, -2, 1)))        # 44 bits
    tail = [big, Group(((0, 0, 0),) * 3), LAST]
    base = dict(flat_restart='next-cycle', threshold_eq='lower',
                frac_reset='chunk', delay_offset='inclusive')

    def fullness_after_29(groups):
        m = Model(p, dict(base))
        m.rc_step = lambda g, n, a, i, m=m: record(m, n, a)
        m.bits = 0
        for g, s in enumerate(groups):
            m.group(g, s)
        return m.bits

    def record(m, n, actual):
        # Buffer arithmetic only (no QP decisions), to size the early groups.
        m.fullness += actual
        for _ in range(n):
            m.pixels += 1
            m.fullness -= m.drain()
        if m.pixels == 90:
            m.bits = m.fullness

    # Group sizes before group 29 set its fullness. A luma suffix one bit
    # wider adds 4 bits; a size-4 residual followed by a group coded at the
    # raised predicted size adds 7. Early additions keep the buffer
    # non-negative after draining starts at pixel 33.
    groups = [small(+1 if g % 2 == 0 else -1) for g in range(29)] + tail
    delta = 63 - fullness_after_29(groups)
    pairs = next(j for j in range(4) if delta - 7 * j >= 0 and (delta - 7 * j) % 4 == 0)
    steps = (delta - 7 * pairs) // 4
    for j in range(pairs):
        g = 2 + 2 * j
        groups[g] = Group(((3, -3, 5), (0, 0, 0), (0, 0, 0)))          # sizes 3,3,4
        groups[g + 1] = Group(groups[g + 1].res, width=(4, None, None))
    for g in [0, 1] + list(range(2 + 2 * pairs, 29)):
        extra = min(steps, 4)          # luma width at most 7: 28 bits, in target
        if extra:
            groups[g] = Group(groups[g].res, width=(3 + extra, None, None))
            steps -= extra
    assert steps == 0
    assert fullness_after_29(groups) == 63
    return 'oq3_fractional_bpp', 'frac_reset', p, groups


def oq1_flat_restart():
    """Somewhat-flat override at group 30, QP 8 -> 4, flagged at group 27.

    Ranges 0-13 pin QP 8 (range 14: 8 to 15, so overrides are not
    suppressed). Group 29 is in target, so its short-term cycle keeps
    prevQp. Next-cycle reading: that cycle already produced 8 for group 31.
    In-flight reading: it is re-run from prevQp 4 and produces 4.
    """
    p = Params(bpp16=128, xmit_delay=128, flat_min=3, flat_max=12,
               ranges=((8, 8, 0),) * 14 + ((8, 15, 0),))
    groups = []
    for g in range(29):
        # QP 8: qLevel (3, 5); luma residuals of size 3 fit the 5-bit maximum.
        groups.append(small(+1 if g % 2 == 0 else -1, flat_flag=1 if g == 27 else 0))
    groups[28] = Group(groups[28].res, flat_type=0, flat_pos=1)   # -> group 30
    groups.append(Group(((7, -8, 7), (1, -2, 1), (0, 0, 0))))     # 24 bits, in target
    groups.append(Group(((0, 0, 0),) * 3))
    groups.append(LAST)
    return 'oq1_flat_restart', 'flat_restart', p, groups


def build(name, question, p, groups):
    results = {}
    reference = None
    for readings in all_readings():
        m, units = simulate(p, groups, readings)
        if reference is None:
            reference = units
        assert units == reference, (name, 'parse depends on readings', readings)
        own = readings[question]
        if own in results:
            assert results[own][0].rgb() == m.rgb(), (name, 'depends on another question', readings)
        else:
            results[own] = (m, readings)
    first, second = READINGS[question]
    assert results[first][0].rgb() != results[second][0].rgb(), (name, 'readings agree')
    payload, used, _ = multiplex(reference, p.chunk)
    OUT.mkdir(exist_ok=True)
    (OUT / f'{name}.pps').write_bytes(p.pps())
    (OUT / f'{name}.bin').write_bytes(payload)
    (OUT / f'{name}.syntax.txt').write_text(''.join(' '.join(u) + '\n' for u in reference))
    entry = dict(question=question, width=p.width, height=1, mux_bits=used,
                 payload_bits=8 * p.chunk, initial_offset=p.initial_offset,
                 pps_sha256=hashlib.sha256(p.pps()).hexdigest(),
                 payload_sha256=hashlib.sha256(payload).hexdigest(), readings={})
    for value in READINGS[question]:
        m, _ = results[value]
        ppm = f'P6\n{p.width} 1\n255\n'.encode() + m.rgb()
        (OUT / f'{name}.{value}.expected.ppm').write_bytes(ppm)
        entry['readings'][value] = dict(
            expected_sha256=hashlib.sha256(ppm).hexdigest(),
            qp_schedule=[row['qp'] for row in m.log],
            last_group_rgb=[list(m.rgb()[3 * x:3 * x + 3]) for x in range(p.width - 3, p.width)],
            decisive=[m.log[g] for g in (29, 30, 31)])
    return entry


def main():
    manifest = {}
    for make in (oq1_flat_restart, oq2_threshold_equality, oq3_fractional_bpp):
        name, question, p, groups = make()
        manifest[name] = build(name, question, p, groups)
    (OUT / 'manifest.json').write_text(json.dumps(manifest, indent=1) + '\n')
    print(json.dumps(manifest, indent=1))


if __name__ == '__main__':
    main()
