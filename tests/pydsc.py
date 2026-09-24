#!/usr/bin/env python3
"""A small DSC decoder model in Python, for building discriminators.

Written from DSC 1.1 and DSC 1.2b, separately from the C decoder in src/,
and used only by the test generators. Scope: one slice, DSC 1.1 and 1.2,
CBR; RGB 4:4:4 at 8 to 16 bpc, and YCbCr: 4:4:4, simple 4:2:2 and the
native 4:2:2 and 4:2:0 containers of DSC 1.2b §3.10.1 and §6.1. Block
prediction is modelled for YCbCr only (the native 4:2:0 question OQ-39
needs it); RGB inputs are built without it. The questions of RESEARCH.md
take their reading from a dict; every other question uses the reading the
C decoder defaults to.

Besides decoding bits (Decoder.decode), it builds them (Builder): a builder
keeps the same state as the decoder and turns a chosen coding of each group
(residuals, MPP or ICH indices) into the bit strings of its three units, so
that a test can steer the rate control group by group.
"""

READINGS = {
    'delay_partial': ('pixels', 'group-end'),
    'bpg_combine': ('replace', 'add'),
    'chroma_qlevel': ('table', 'equal-depth'),
    'prefix16': ('15', '13'),
    'bitsave_ich': ('not', 'set'),
    'bitsave_pred': ('raw', 'adjusted', 'next'),
    'bitsave_flat': ('supergroup', 'group', 'received', 'carrier', 'span', 'lagged'),
    'line_flat': ('very', 'signaled'),
    'low_min': ('max-qp', 'min-qp'),
    'decrement_test': ('both', 'size'),
    'activity_qp': ('prev', 'prev2'),
    'bitsave_step': ('1', '2'),
    'target_floor': ('none', 'zero'),
    'flat_rerun': ('changed', 'every'),
    'rerun_bitsave': ('keep', 'redo'),
    'mux16': ('68', '64'),
    'flat_top': ('equal', 'at-or-above'),
    'prefix16_scope': ('qp0', 'qlevel'),
    'prefix16_cut': ('always', 'longer'),
    'activity420': ('luma', 'sum'),
    'activity422': ('sizes', 'total'),
    'bp420_edge': ('luma', 'all'),
    'offset_adj': ('subtract', 'start'),
    'ich_window': ('pixels', 'container'),
    'scale_first': ('group', 'not'),
    'scale_line': ('until-unity', 'first'),
}
# The C decoder's defaults (src/options.c).
DEFAULTS = {'delay_partial': 'group-end', 'bpg_combine': 'add', 'chroma_qlevel': 'equal-depth',
            'prefix16': '13', 'bitsave_ich': 'not', 'bitsave_pred': 'next',
            'bitsave_flat': 'lagged', 'line_flat': 'signaled',
            'low_min': 'min-qp', 'decrement_test': 'size', 'activity_qp': 'prev2',
            'bitsave_step': '2', 'target_floor': 'zero', 'flat_rerun': 'every',
            'rerun_bitsave': 'redo', 'mux16': '64', 'flat_top': 'at-or-above',
            'prefix16_scope': 'qlevel', 'prefix16_cut': 'longer',
            'activity420': 'luma', 'activity422': 'sizes', 'bp420_edge': 'luma',
            'offset_adj': 'start', 'ich_window': 'container', 'scale_first': 'not', 'scale_line': 'first'}
# The readings the text prints, for the questions the model decided against
# the text (OQ-26 to OQ-33) or has not decided (OQ-34).
TEXT = {'low_min': 'max-qp', 'decrement_test': 'both', 'activity_qp': 'prev', 'bitsave_step': '1',
        'target_floor': 'none', 'flat_rerun': 'changed', 'rerun_bitsave': 'keep', 'mux16': '68',
        'flat_top': 'equal', 'prefix16_scope': 'qp0', 'prefix16_cut': 'always'}


def clamp(v, lo, hi):
    return lo if v < lo else hi if v > hi else v


def signed_size(v):
    if v == 0:
        return 0
    w = 1
    while not -(1 << (w - 1)) <= v < (1 << (w - 1)):
        w += 1
    return w


class PPS:
    """The PPS fields these tests use; bytes() writes the 128-byte form."""

    def __init__(self, **kw):
        self.version = 1
        self.bpc = 8
        self.line_buf = 9
        self.bpp16 = 128
        self.width = self.height = 0
        self.slice_width = self.slice_height = 0
        self.xmit_delay, self.dec_delay = 512, 512
        self.scale = 8
        self.scale_inc = self.scale_dec = 0
        self.first_line_bpg = 0
        self.nfl_bpg = self.slice_bpg = 0
        self.initial_offset, self.final_offset = 6144, 8191
        self.flat_min = self.flat_max = 15
        self.model_size = 8192
        self.edge_factor, self.limit0, self.limit1 = 6, 15, 15
        self.tgt_hi = self.tgt_lo = 3
        self.thresholds = (14, 28, 42, 56, 70, 84, 98, 105, 112, 119, 121, 123, 125, 126)
        self.ranges = ((0, 0, 0),) * 15
        self.second_line_bpg = self.nsl_bpg = self.second_line_adj = 0
        self.rgb, self.simple_422, self.native_422, self.native_420 = 1, 0, 0, 0
        self.block_pred = 0
        self.__dict__.update(kw)
        self.slice_width = self.slice_width or self.width
        self.slice_height = self.slice_height or self.height

    @property
    def coded_width(self):
        """The container's width in native modes (DSC 1.2b §4.3)."""
        return self.slice_width >> 1 if self.native_422 or self.native_420 else self.slice_width

    @property
    def chunk(self):
        return (self.coded_width * self.bpp16 + 127) // 128

    def bytes(self):
        p = bytearray(128)

        def word(i, v):
            p[i:i + 2] = v.to_bytes(2, 'big')
        p[0] = 0x10 | self.version
        p[3] = ((self.bpc & 15) << 4) | (self.line_buf & 15)
        p[4] = (0x20 if self.block_pred else 0) | (0x10 if self.rgb else 0) | \
            (0x08 if self.simple_422 else 0) | (self.bpp16 >> 8)
        p[5] = self.bpp16 & 0xff
        for i, v in [(6, self.height), (8, self.width), (10, self.slice_height),
                     (12, self.slice_width), (14, self.chunk), (16, self.xmit_delay),
                     (18, self.dec_delay), (22, self.scale_inc), (24, self.scale_dec),
                     (28, self.nfl_bpg), (30, self.slice_bpg), (32, self.initial_offset),
                     (34, self.final_offset), (38, self.model_size)]:
            word(i, v)
        p[21] = self.scale
        p[27] = self.first_line_bpg
        p[36], p[37] = self.flat_min, self.flat_max
        p[40], p[41], p[42] = self.edge_factor, self.limit0, self.limit1
        p[43] = (self.tgt_hi << 4) | self.tgt_lo
        p[44:58] = bytes(self.thresholds)
        for i, (lo, hi, off) in enumerate(self.ranges):
            word(58 + 2 * i, (lo << 11) | (hi << 6) | (off & 63))
        if self.version == 2:
            p[88] = (2 if self.native_420 else 0) | (1 if self.native_422 else 0)
            p[89] = self.second_line_bpg
            word(90, self.nsl_bpg)
            word(92, self.second_line_adj)
        return bytes(p)


class Format:
    """Units of a group, in substream order (DSC 1.2b §4.4, §6.1): Y, Co, Cg
    (or Y, Cb, Cr); native 4:2:2 Y even, Cb, Cr, Y odd; native 4:2:0 Y even,
    Y odd, chroma (Cb on even lines, Cr on odd ones)."""

    def __init__(self, pps, readings):
        b = self.bpc = pps.bpc
        self.version = pps.version
        self.rgb = bool(pps.rgb)
        self.native = '422' if pps.native_422 else '420' if pps.native_420 else None
        self.simple_422 = bool(pps.simple_422)
        self.units = 4 if self.native == '422' else 3
        cd = (b + 1 if b < 16 else 16) if self.rgb else b
        self.depth = (b,) + (cd if self.native is None else b,) * (self.units - 1)
        self.luma = {'422': (1, 0, 0, 1), '420': (1, 1, 0)}.get(self.native, (1, 0, 0))
        self.odd = {'422': 3, '420': 1}.get(self.native, 0)
        # The unit that carries each of the three ICH indices (§6.6.2).
        self.index_unit = (3, 1, 2) if self.native == '422' else (0, 1, 2)
        # The units BP predicts (§6.4.2).
        self.bp_units = (0, 1) if self.native == '420' else tuple(range(self.units))
        self.mux = 48 if b <= 10 else 64
        self.max_se = (4 * b + 4,) + tuple(4 * d for d in self.depth[1:])
        if b == 16 and readings['mux16'] == '64':
            self.max_se = (self.mux,) + self.max_se[1:]
        self.max_qp = 15 + 2 * (b - 8)
        self.flat_type_qp = 7 + 2 * (b - 8)
        self.very_flat_qp = 1 + 2 * (b - 8)
        # §6.8.6: DSC 1.2 lowers qLevelC by one where the luma and chroma
        # depths are equal: YCbCr always, RGB at 16 bpc (OQ-20).
        self.chroma_adjust = self.version == 2 and (
            not self.rgb or (self.depth[1] == self.depth[0] and readings['chroma_qlevel'] == 'equal-depth'))

    def qlevel(self, qp, unit):
        y = (qp - 1) // 2 if qp else 0
        c = qp // 2 + 1 if qp else 0
        if qp + 2 == self.max_qp:
            y, c = y - 1, c + 1
        if self.chroma_adjust and c:
            c -= 1
        return y if self.luma[unit] else c


class RateControl:
    """DSC 1.1 §6.8 with the readings the decoder defaults to, and the DSC
    1.2b changes of §6.8.2, §6.8.4 and §6.8.5.2."""

    def __init__(self, pps, fmt, readings):
        self.p, self.f, self.r = pps, fmt, readings
        self.v12 = pps.version == 2
        self.w = pps.coded_width          # sliceWidth in container pixels (§6.8.1)
        self.fullness = 0
        self.offset = (pps.initial_offset - pps.model_size) * 2048
        # OQ-40: Table E-2 adds second_line_offset_adj at the slice start.
        if self.v12 and readings['offset_adj'] == 'start':
            self.offset += pps.second_line_adj * 2048
        self.overflow = -224 if fmt.native == '422' else -172   # §6.8.4
        self.pixels = self.groups = 0
        self.delay_end = 0                 # end of the previous group, three pixels a group
        self.frac = self.chunk_bits = self.chunk_pixels = 0
        self.scale, self.scale_clock = pps.scale, 0
        self.increasing = self.increase_next = self.clamped = False
        self.qp = self.pending = self.last = self.prev2 = self.used = 0
        self.prev_ideal = 0
        self.lag = None
        self.saved = None
        self.bit_save = self.mpp_state = 0
        self.step_state = None
        self.log = []

    # §6.8.4, Figures 6-12/6-13 (DSC 1.1), 6-17/6-18 (DSC 1.2b)
    def short_term(self, i, prev, prev2):
        p = self.p
        lo, hi = i['target'] - p.tgt_lo, i['target'] + p.tgt_hi

        def increment(min_qp, max_qp):
            cur = max(min_qp, prev)
            edge = i['ideal'] * 2 < i['prev_ideal'] * p.edge_factor
            if cur == prev2:
                permit = edge
            elif cur > prev2:                       # OQ-5: swapped
                permit = edge and cur < p.limit0
            else:
                permit = cur < p.limit1
            return min(max_qp, cur + ((i['actual'] - i['target']) >> 1)) if permit else cur

        top14 = p.ranges[14][1]
        if not i['v12']:
            if i['model'] > self.overflow:
                return top14
            if i['ideal'] == 3:
                return max(prev - 1, 0, i['min'] // 2)
            if i['actual'] < lo and i['ideal'] < lo:
                return max(prev - 1, 0, i['min'])
            if i['actual'] > hi and i['fullness'] >= 64:
                return increment(i['min'], i['max'])
            return prev
        min_qp, max_qp = i['min'], i['max']
        if i['model'] > self.overflow:
            st = max_qp = top14
        elif i['fullness'] < 192:
            st = min_qp
        elif i['bit_save'] == 2:
            step = 2 if self.r['bitsave_step'] == '2' else 1
            st, max_qp = prev + step, min(2 * self.f.bpc - 1, i['max'] + 1)
        elif i['bit_save'] == 1:
            st, max_qp = prev, min(2 * self.f.bpc - 1, i['max'] + 1)
        elif i['zero']:
            st, min_qp = prev - 1, max((i['min'] if self.r['low_min'] == 'min-qp' else i['max']) - 4, 0)
        elif i['ideal'] < lo and (self.r['decrement_test'] == 'size' or i['actual'] < lo):
            st = prev - 1
        elif i['actual'] > hi and i['fullness'] >= 64:
            st = increment(min_qp, max_qp)
        else:
            st = prev
        return clamp(st, min_qp, max_qp)

    # §6.8.5.2, with OQ-1 in-flight, OQ-16 previous-qp, OQ-18 previous; DSC
    # 1.2 line starts (OQ-25)
    def flatness(self, flat, very_flat, line_start):
        always_very = False
        if self.v12 and line_start:
            flat = very_flat = True
            always_very = self.r['line_flat'] == 'very'
        if not flat:
            return
        q = self.qp
        top = self.p.ranges[14][1]
        # DSC 1.2b §6.8.5.2: a line start is adjusted only below range 14's
        # maximum; OQ-34 for signaled flatness.
        if self.v12 and (line_start or self.r['flat_top'] == 'at-or-above'):
            if self.used >= top:
                return
        elif self.used == top:
            return
        demote = False if always_very else self.used < self.f.flat_type_qp
        adjust = (lambda v: max(v - 4, 0)) if (not very_flat or demote) else (lambda v: self.f.very_flat_qp)
        master = adjust(q)
        if master == q and not (self.v12 and self.r['flat_rerun'] == 'every'):
            return
        self.qp = master
        prev2 = self.used
        if self.v12:
            prev2 = adjust(prev2)                     # DSC 1.2b §6.8.4
            if self.r['rerun_bitsave'] == 'redo' and self.saved and self.step_state:
                y, g, self.bit_save, self.mpp_state = self.step_state
                self.bit_save_update(y, g, master, prev2)
                self.saved = dict(self.saved, bit_save=self.bit_save)
        redo = self.short_term(self.saved, master, prev2) if self.saved else self.pending
        self.pending, self.prev2, self.last = redo, master, redo

    def drain(self):
        p = self.p
        if self.pixels < p.xmit_delay:
            return 0
        self.frac += p.bpp16
        removed, self.frac = self.frac >> 4, self.frac & 15
        self.chunk_bits += removed
        self.chunk_pixels += 1
        if self.chunk_pixels == self.w:
            pad = 8 * p.chunk - self.chunk_bits
            assert 0 <= pad <= 8
            removed += pad
            self.chunk_bits = self.chunk_pixels = 0
            self.frac = 0
        return removed

    def bit_save_update(self, y, g, prev, prev2):
        pr = g['predicted']
        qp = prev2 if self.r['activity_qp'] == 'prev2' else prev
        # §6.8.4 predActivity; OQ-37 and OQ-38 for the native forms.
        if self.f.native == '420':
            activity = (qp + max(pr[0], pr[1] + pr[2]) if self.r['activity420'] == 'sum'
                        else qp + max(pr[0], pr[1]) + pr[2])
        elif self.f.native == '422':
            activity = ((qp + sum(pr[:4])) >> 1 if self.r['activity422'] == 'total'
                        else qp + (sum(pr[:4]) >> 1))
        else:
            activity = qp + pr[0] + max(pr[1], pr[2])
        thresh = self.f.depth[0] + self.f.depth[1] - 2
        p_mode = g['ich'] if self.r['bitsave_ich'] == 'set' else not g['ich']
        if y == 0 or g['flat']:
            self.bit_save = self.mpp_state = 0
        elif p_mode and g['mpp'] >= 3:
            self.mpp_state = min(self.mpp_state + 1, 2)
            if self.mpp_state >= 2:
                self.bit_save = 2
        elif p_mode and activity >= thresh:
            pass
        elif g['ich']:
            self.bit_save = max(1, self.bit_save)
        else:
            self.bit_save = self.mpp_state = 0

    def step(self, y, n, g):
        p = self.p
        before = self.pixels
        self.fullness += g['actual']
        for _ in range(n):
            self.pixels += 1
            self.fullness -= self.drain()
        assert self.fullness >= 0, 'rate buffer underflow'
        if self.increase_next:
            self.scale, self.scale_clock = 9, 0
            self.increasing, self.increase_next = True, False
        elif self.increasing:
            self.scale_clock += 1
            if self.scale_clock >= p.scale_inc:
                self.scale_clock, self.scale = 0, self.scale + 1
        elif self.scale > 8 and (y == 0 or self.r['scale_line'] == 'until-unity'):
            # OQ-14 from-group-0; OQ-42 (a decrement due at group 0) and
            # OQ-43 (only during the first line).
            self.scale_clock += 1
            if self.scale_clock >= p.scale_dec:
                self.scale_clock = 0
                if self.groups or self.r['scale_first'] == 'group':
                    self.scale -= 1
        end = p.xmit_delay
        if self.r['delay_partial'] == 'group-end':
            delayed = max(0, min(before + 3, end) - min(self.delay_end, end))
        else:
            delayed = min(n, max(0, end - before))
        self.delay_end = before + 3
        delta = p.slice_bpg + (p.nfl_bpg if y else -p.first_line_bpg * 2048)
        delta -= delayed * p.bpp16 * 128
        if self.v12:
            delta += -p.second_line_bpg * 2048 if y == 1 else p.nsl_bpg
            if y == 1 and before == self.w:
                delta -= p.second_line_adj * 2048
        self.offset += delta
        limit = (p.final_offset - p.model_size) * 2048
        if self.offset < limit:
            self.clamped = True
        if self.clamped and self.offset > limit:
            self.offset = limit
        offset = self.offset >> 11
        if (not self.increasing and not self.increase_next and p.scale_inc and y and
                self.pixels >= p.xmit_delay and offset > -p.model_size):
            self.increase_next = True
        model = ((self.fullness + offset) * self.scale) >> 3
        assert model <= 0, 'rcModelFullness above 0'
        rng = sum(model > t * 64 - p.model_size for t in p.thresholds)
        used = 0 if self.lag is None else self.lag          # OQ-11: range lag
        self.lag = rng
        lo_qp, hi_qp, off = p.ranges[used]
        off = off - 64 if off & 32 else off
        xform = -(p.nfl_bpg >> 11) if y else p.first_line_bpg
        if self.v12:
            second = p.second_line_bpg if y == 1 else -(p.nsl_bpg >> 11)
            xform = xform + second if self.r['bpg_combine'] == 'add' else second
        target = (n * p.bpp16 + 8) // 16 + off + xform - (p.slice_bpg >> 11)
        if self.v12 and self.r['target_floor'] == 'zero':
            target = max(target, 0)
        if self.v12:
            self.step_state = (y, g, self.bit_save, self.mpp_state)
            self.bit_save_update(y, g, self.last, self.qp)
        # The overflow test uses bufferFullness + rcXformOffset, unscaled.
        i = dict(fullness=self.fullness, model=self.fullness + offset, target=target, actual=g['actual'],
                 ideal=g['ideal'], prev_ideal=self.prev_ideal, min=lo_qp, max=hi_qp,
                 v12=self.v12, zero=g['zero'], bit_save=self.bit_save)
        nxt = self.short_term(i, self.last, self.prev2)
        self.log.append(dict(group=self.groups, qp=self.qp, actual=g['actual'], ideal=g['ideal'],
                             fullness=self.fullness, model=model, range=rng, range_used=used,
                             bit_save=self.bit_save, generated=nxt))
        self.saved, self.used = i, self.qp
        self.prev2, self.last, self.prev_ideal = self.last, nxt, g['ideal']
        self.qp, self.pending = self.pending, nxt
        self.groups += 1


class Slice:
    """Shared state of a decoder and a builder: size prediction, flatness
    signaling, rate control and reconstruction."""

    CANDIDATES = (-1, -3, -4, -5, -6, -7, -8, -9, -10)

    def __init__(self, pps, readings=None):
        self.p = pps
        self.r = dict(DEFAULTS, **(readings or {}))
        self.f = Format(pps, self.r)
        self.rc = RateControl(pps, self.f, self.r)
        u = self.f.units
        self.predicted, self.last_level = [0] * u, [0] * u
        self.was_ich = False
        self.flag = self.sg_flag = self.prev_flag = 0
        self.flat_group, self.flat_type = None, 0
        w = self.w = pps.coded_width
        mids = tuple(1 << (d - 1) for d in self.f.depth)
        self.prev_line = [mids] * w
        self.older = [mids] * w          # the line two up: native 4:2:0 chroma
        self.first_lines = 2 if self.f.native == '420' else 1
        self.cur = [None] * w
        self.history = []
        self.last = [0] * u
        self.rows = []
        self.bp_count = 0
        self.bp_log = []
        self.g = 0

    # --- reconstruction (§6.4, §6.5, §7.5, §7.6) ---------------------------
    def ref(self, c):
        """The line unit c is predicted from (DSC 1.2b §6.4.1.2)."""
        return self.older if self.f.native == '420' and c == 2 else self.prev_line

    def has_above(self, y, c):
        return y >= (2 if self.f.native == '420' and c == 2 else 1)

    def above(self, k, c):
        return self.ref(c)[clamp(k, 0, self.w - 1)][c]

    def bp_sample(self, k, c):
        # OQ-4 midpoint: previous-line samples left of the slice.
        return 1 << (self.f.depth[c] - 1) if k < 0 else self.above(k, c)

    def bp_search(self, x):
        """§6.4.4.1 on the container, over the units BP predicts."""
        best, vector = None, -1
        for cand in self.CANDIDATES:
            total = 0
            for b in range(3):
                part = 0
                for j in range(3):
                    pos = x - 6 + 3 * b + j
                    for c in self.f.bp_units:
                        d = abs(self.bp_sample(pos, c) - self.bp_sample(pos + cand, c))
                        part += min(63, d >> (self.f.depth[c] - 7))
                total += min(511, part)
            sad = total >> 3                    # OQ-10: shift
            if best is None or sad < best:
                best, vector = sad, cand
        return vector

    def recent_edge(self, x, y):
        """lastEdgeCount < 3 read at hPos + 2 (OQ-13 window); OQ-39 for the
        chroma of native 4:2:0."""
        edge = 32 << (self.f.bpc - 8)
        for q in range(x, x + 3):
            for c in range(self.f.units):
                if (self.f.native == '420' and c not in self.f.bp_units and
                        (self.r['bp420_edge'] == 'luma' or not self.has_above(y, c))):
                    continue
                if abs(self.bp_sample(q, c) - self.bp_sample(q - 1, c)) > edge:
                    return True
        return False

    def ich_above(self, x, i):
        """ICH entries 25 to 31 (§6.5.1; native: DSC 1.2b Figures 6-7, 6-8)."""
        f, w = self.f, self.w
        if f.native is None:
            return self.prev_line[clamp(x - 2, 0, w - 7) + i - 25]
        # OQ-41: the eight luma samples, or the container pixels x - 1 to
        # x + 3, shifted into the slice.
        if self.r['ich_window'] == 'container':
            q = 2 * clamp(x, 1, w - 4) - 1 + i - 25
        else:
            q = clamp(2 * x - 1, 0, 2 * w - 8) + i - 25

        def luma(k):
            return self.prev_line[k >> 1][f.odd if k & 1 else 0]
        px = [0] * f.units
        px[0], px[f.odd] = luma(q), luma(q + 1)
        for c in range(1, f.units):
            if not f.luma[c]:
                px[c] = self.ref(c)[(q + 1) >> 1][c]
        return tuple(px)

    def reconstruct(self, x, y, levels, res, mpp, ich, idx):
        f, w, units = self.f, self.w, self.f.units
        n = min(3, w - x)
        out = [[0] * 3 for _ in range(units)]
        cap = 25 if y >= self.first_lines else 32
        use_bp, vector = False, 0
        if self.p.block_pred and y:
            if x == 0:
                self.bp_count = 0
            vector = self.bp_search(x)
            if vector == -1:
                self.bp_count = 0
            elif x >= 9 and self.bp_count < 3:
                self.bp_count += 1
            use_bp = vector != -1 and self.bp_count >= 3 and n == 3 and self.recent_edge(x, y)
            self.bp_log.append((y, x, vector, self.bp_count, use_bp))
        if ich:
            for j in range(n):
                i = idx[j]
                if i < cap:
                    assert i < len(self.history), ('ICH index to an empty entry', i)
                    px = self.history[i]
                else:
                    px = self.ich_above(x, i)
                for c in range(units):
                    out[c][j] = px[c]
        else:
            for c in range(units):
                mid, top, q = 1 << (f.depth[c] - 1), (1 << f.depth[c]) - 1, levels[c]
                lim = (1 << q) // 2
                first = not self.has_above(y, c)
                bp = use_bp and c in f.bp_units

                def blend(k):
                    filt = (self.above(k - 1, c) + 2 * self.above(k, c) + self.above(k + 1, c) + 2) >> 2
                    return self.above(k, c) + clamp(filt - self.above(k, c), -lim, lim)
                a = self.cur[x - 1][c] if x else mid
                cblend = blend(x - 1) if x and not first else mid
                lo_b = hi_b = a
                cum = 0
                for j in range(n):
                    r = res[c][j] << q
                    if mpp[c]:
                        pred = mid + (self.last[c] & ((1 << q) - 1))
                    elif bp:
                        pred = self.cur[x + j + vector][c]
                    elif first:
                        pred = clamp(a + cum, 0, top)
                    else:
                        b = blend(x + j)
                        lo_b, hi_b = min(lo_b, b), max(hi_b, b)
                        pred = clamp(a + b - cblend + cum, lo_b, hi_b)
                    out[c][j] = clamp(pred + r, 0, top)
                    cum += r
        for j in range(n):
            self.cur[x + j] = tuple(out[c][j] for c in range(units))
        self.last = [out[c][n - 1] for c in range(units)]
        if x + n < w:   # §6.5.2: no update from a line's last group
            if ich:
                keep = [idx[j] for j in range(3) if idx[j] not in idx[j + 1:]]
                new = [self.history[i] if i < cap else self.ich_above(x, i) for i in keep]
                rest = [e for k, e in enumerate(self.history) if k not in idx]
                self.history = (list(reversed(new)) + rest)[:cap]
            else:
                self.history = ([self.cur[x + 2], self.cur[x + 1], self.cur[x]] + self.history)[:cap]
        if x + n == w:
            shift = [max(0, d - self.p.line_buf) for d in f.depth]
            self.older = self.prev_line
            self.prev_line = [tuple(min((v + ((1 << s) >> 1)) >> s, (1 << self.p.line_buf) - 1) << s
                                    for v, s in zip(px, shift)) for px in self.cur]
            self.rows.append(list(self.cur))
            if y + 1 >= self.first_lines:
                self.history = self.history[:25]

    def rgb(self):
        """§7.7 inverse conversion of the decoded slice, raster order."""
        f, top = self.f, (1 << self.f.bpc) - 1
        out = []
        for row in self.rows[:self.p.height]:
            for y_, co, cg in row[:self.p.width]:
                if f.bpc == 16:
                    co, cg = (co - 0x8000) * 2, (cg - 0x8000) * 2
                else:
                    co, cg = co - (1 << f.bpc), cg - (1 << f.bpc)
                t = y_ - (cg >> 1)
                b = t - (co >> 1)
                out.append((clamp(co + b, 0, top), clamp(cg + t, 0, top), clamp(b, 0, top)))
        return out

    def ppm(self):
        top = (1 << self.f.bpc) - 1
        body = b''.join(bytes(px) if top < 256 else b''.join(v.to_bytes(2, 'big') for v in px)
                        for px in self.rgb())
        return f'P6\n{self.p.width} {self.p.height}\n{top}\n'.encode() + body

    def planes(self):
        """Y, Cb, Cr planes of a YCbCr slice, cropped to the picture: chroma at
        half width for 4:2:2 and also half height for 4:2:0 (§7.7, Annex B,
        DSC 1.2b Figures 3-12 and 3-14)."""
        f, W, H = self.f, self.p.width, self.p.height
        cw = (W + 1) // 2 if f.native or f.simple_422 else W
        ch = (H + 1) // 2 if f.native == '420' else H
        Y = [[0] * W for _ in range(H)]
        C = [[[0] * cw for _ in range(ch)] for _ in range(2)]
        for y, row in enumerate(self.rows):
            for x, px in enumerate(row):
                if f.native:
                    for k, v in ((2 * x, px[0]), (2 * x + 1, px[f.odd])):
                        if y < H and k < W:
                            Y[y][k] = v
                    if x < cw:
                        if f.native == '422':
                            if y < H:
                                C[0][y][x], C[1][y][x] = px[1], px[2]
                        elif y // 2 < ch:
                            C[y % 2][y // 2][x] = px[2]
                elif y < H and x < W:
                    Y[y][x] = px[0]
                    if not f.simple_422:
                        C[0][y][x], C[1][y][x] = px[1], px[2]
                    elif x % 2 == 0:
                        C[0][y][x // 2], C[1][y][x // 2] = px[1], px[2]
        return Y, C[0], C[1]

    def yuv(self):
        """The raw layouts of the reference model's .yuv files (its README.TXT):
        planar 4:2:0, UYVY 4:2:2; 4:4:4 planar as dscdecode writes it. Above
        8 bits, two bytes per sample, least significant first, the sample in
        the most significant bits."""
        Y, Cb, Cr = self.planes()
        shift = 16 - self.f.bpc

        def enc(vals):
            return b''.join((v << shift).to_bytes(2, 'little') for v in vals) if shift < 8 else bytes(vals)
        if self.f.native == '422' or self.f.simple_422:
            out = []
            for y, row in enumerate(Y):
                for x in range(0, len(row), 2):
                    out += [Cb[y][x // 2], row[x], Cr[y][x // 2], row[x + 1] if x + 1 < len(row) else row[x]]
            return enc(out)
        return b''.join(enc([v for row in plane for v in row]) for plane in (Y, Cb, Cr))

    def output(self):
        """The decoded picture as dscdecode writes it: PPM or raw YCbCr."""
        return self.ppm() if self.f.rgb else self.yuv()

    # --- per group bookkeeping shared by decode and build ------------------
    def begin(self):
        g, w = self.g, self.w
        gpl = (w + 2) // 3
        y, x = g // gpl, (g % gpl) * 3
        self.rc.flatness(self.flat_group == g, self.flat_type, x == 0 and y > 0)
        self.prev_flag = self.flag
        if g % 4 == 1:
            self.sg_flag = self.flag
        return x, y, self.rc.qp

    def finish(self, x, y, qp, levels, res, mpp, ich, idx, actual, ideal, pred_raw, pred_adj):
        n = min(3, self.w - x)
        self.reconstruct(x, y, levels, res, mpp, ich, idx)
        chosen = {'raw': pred_raw, 'adjusted': pred_adj, 'next': list(self.predicted)}
        g = dict(actual=actual, ideal=ideal, ich=ich, mpp=sum(map(bool, mpp)) if not ich else 0,
                 zero=not ich and ideal == self.f.units, predicted=chosen[self.r['bitsave_pred']],
                 flat={'supergroup': self.sg_flag, 'group': self.flat_group == self.g,
                       'received': self.flag,
                       'carrier': self.flag and self.g % 4 in (3, 0),
                       'span': self.flag or self.sg_flag,
                       'lagged': self.prev_flag}[self.r['bitsave_flat']])
        self.rc.step(y, n, g)
        self.g += 1


class Builder(Slice):
    """Emits the unit strings of each group for a chosen coding."""

    def __init__(self, pps, readings=None):
        super().__init__(pps, readings)
        self.units = []

    def group(self, res=None, mpp=None, ich=None, flat=None, widths=None):
        """res: one residual triple per unit, quantized; mpp: per unit; ich:
        three indices; flat: (flag, type, position) to send where the syntax
        allows; widths: optional suffix sizes per unit."""
        x, y, qp = self.begin()
        f = self.f
        U = f.units
        mpp = tuple(mpp) if mpp is not None else (False,) * U
        bits = [''] * U
        g = self.g
        if g % 4 == 3:
            self.flag = 0
            if self.p.flat_min <= qp <= self.p.flat_max:
                self.flag = flat[0] if flat else 0
                bits[0] += str(self.flag)
            else:
                assert not flat, ('flatness flag not sendable', g)
        elif g % 4 == 0 and self.flag:
            ftype, pos = (flat[1], flat[2]) if flat else (0, 0)
            self.flat_type = ftype if qp >= f.flat_type_qp else 0
            if qp >= f.flat_type_qp:
                bits[0] += str(ftype)
            bits[0] += format(pos, '02b')
            self.flat_group = g + 1 + pos
        levels = [f.qlevel(qp, u) for u in range(U)]
        limited = f.version == 2 and f.bpc == 16 and (f.qlevel(qp, 0) <= 1 if self.r['prefix16_scope'] == 'qlevel' else qp == 0)
        pred_raw, pred_adj = list(self.predicted), [0] * U
        ideal, actual = 0, 0
        for u in range(U):
            mx = f.depth[u] - levels[u]
            pred = clamp(self.predicted[u] + self.last_level[u] - levels[u], 0, mx - 1)
            pred_adj[u] = pred
            # DSC 1.2b Table 4-10, §3.10.2: a cut 16 bpc luma prefix (OQ-21,
            # OQ-35, OQ-36): all zeros means MPP, and no ICH.
            limit = mx - pred + 1
            most = 15 if levels[0] else (13 if self.r['prefix16'] == '13' else 15)
            cut = u == 0 and limited and (self.r['prefix16_cut'] == 'always' or limit > most)
            cap = most if cut else limit
            if ich is not None:
                assert not cut, 'no ICH under a cut prefix'
                if u == 0:
                    bits[0] += '1' if self.was_ich else '0' * (mx - pred + 1)
                for k in range(3):
                    if f.index_unit[k] == u:
                        bits[u] += format(ich[k], '05b')
            else:
                need = [signed_size(v) for v in res[u]]
                width = mx if mpp[u] else max(pred, max(need)) if not widths or widths[u] is None else widths[u]
                assert max(need) <= width <= mx and width >= pred and (mpp[u] or width < mx), \
                    ('size', g, u, pred, need, width, mx)
                if cut:
                    z = width - pred
                    assert z < cap or width == mx, ('cut prefix', g, pred, width)
                    bits[0] += '0' * cap if z >= cap else '0' * z + '1'
                elif u == 0 and self.was_ich:
                    bits[0] += '0' * (width - pred + 1) + ('1' if width < mx else '')
                elif u == 0:
                    bits[0] += '0' * (width - pred) + '1'
                else:
                    bits[u] += '0' * (width - pred) + ('1' if width < mx else '')
                if width:
                    bits[u] += ''.join(format(v & ((1 << width) - 1), f'0{width}b') for v in res[u])
                req = [mx] * 3 if mpp[u] else need
                self.predicted[u] = (req[0] + req[1] + 2 * req[2] + 2) >> 2
                ideal += 3 * max(req) + 1
            self.last_level[u] = levels[u]
        if ich is not None:
            ideal = 16
        self.was_ich = ich is not None
        actual = sum(map(len, bits))
        self.units.append(bits)
        self.finish(x, y, qp, levels, res or [[0] * 3] * U, list(mpp), ich is not None,
                    list(ich or (0, 0, 0)), actual, ideal, pred_raw, pred_adj)
        return self.rc.log[-1]

    def payload(self):
        """Table 4-6 multiplexing and zero padding to the slice size."""
        f, p = self.f, self.p
        U = f.units
        streams = [''.join(u[c] for u in self.units) for c in range(U)]
        off, full, words = [0] * U, [0] * U, []
        for u in self.units:
            for c in range(U):
                if full[c] < f.max_se[c]:
                    words.append(streams[c][off[c]:off[c] + f.mux].ljust(f.mux, '0'))
                    off[c] += f.mux
                    full[c] += f.mux
            for c in range(U):
                full[c] -= len(u[c])
                assert full[c] >= 0
        raw = ''.join(words)
        cap = p.chunk * p.slice_height * 8
        assert len(raw) <= cap, ('payload too large', len(raw), cap)
        return int(raw.ljust(cap, '0'), 2).to_bytes(cap // 8, 'big'), len(raw)


class Decoder(Slice):
    """Parses a one-slice payload (§4.4, §4.5, §7.1, §7.2)."""

    def decode(self, payload):
        f, p = self.f, self.p
        U = f.units
        bits = ''.join(format(b, '08b') for b in payload)
        pos, fifo = 0, [''] * U
        gpl = (self.w + 2) // 3

        def take(u, k):
            v = fifo[u][:k]
            assert len(v) == k, 'substream underflow'
            fifo[u] = fifo[u][k:]
            return v
        for _ in range(gpl * p.slice_height):
            for c in range(U):
                if len(fifo[c]) < f.max_se[c]:
                    assert pos + f.mux <= len(bits), 'truncated'
                    fifo[c] += bits[pos:pos + f.mux]
                    pos += f.mux
            start = sum(map(len, fifo))
            x, y, qp = self.begin()
            g = self.g
            if g % 4 == 3:
                self.flag = 0
                if p.flat_min <= qp <= p.flat_max:
                    self.flag = int(take(0, 1))
            elif g % 4 == 0 and self.flag:
                self.flat_type = int(take(0, 1)) if qp >= f.flat_type_qp else 0
                self.flat_group = g + 1 + int(take(0, 2), 2)
            levels = [f.qlevel(qp, u) for u in range(U)]
            limited = f.version == 2 and f.bpc == 16 and (f.qlevel(qp, 0) <= 1 if self.r['prefix16_scope'] == 'qlevel' else qp == 0)
            res, mpp, idx, ich = [[0] * 3 for _ in range(U)], [False] * U, [0, 0, 0], False
            pred_raw, pred_adj, ideal = list(self.predicted), [0] * U, 0
            for u in range(U):
                mx = f.depth[u] - levels[u]
                pred = clamp(self.predicted[u] + self.last_level[u] - levels[u], 0, mx - 1)
                pred_adj[u] = pred
                if u == 0 or not ich:
                    limit = mx - pred + (u == 0)
                    most = 15 if levels[0] else (13 if self.r['prefix16'] == '13' else 15)
                    cut = u == 0 and limited and (self.r['prefix16_cut'] == 'always' or limit > most)
                    cap = most if cut else limit
                    z = 0
                    while z < cap and take(u, 1) == '0':
                        z += 1
                    width = pred + z
                    if u == 0:
                        if cut:
                            if z == cap:
                                width = mx
                        elif self.was_ich:
                            if z == 0:
                                ich = True
                            else:
                                width -= 1
                        elif width == mx + 1:
                            ich = True
                    assert ich or width <= mx, 'size above the maximum'
                if ich:
                    for k in range(3):
                        if f.index_unit[k] == u:
                            idx[k] = int(take(u, 5), 2)
                else:
                    mpp[u] = width == mx
                    for k in range(3):
                        v = int(take(u, width), 2) if width else 0
                        res[u][k] = v - (1 << width) if width and v >> (width - 1) else v
                    req = [mx] * 3 if mpp[u] else [signed_size(v) for v in res[u]]
                    self.predicted[u] = (req[0] + req[1] + 2 * req[2] + 2) >> 2
                    ideal += 3 * max(req) + 1
                self.last_level[u] = levels[u]
            if ich:
                ideal = 16
            self.was_ich = ich
            actual = start - sum(map(len, fifo))
            self.finish(x, y, qp, levels, res, mpp, ich, idx, actual, ideal, pred_raw, pred_adj)
        assert not any('1' in s for s in fifo) and '1' not in bits[pos:], 'nonzero slice padding'
        return self
