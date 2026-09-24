#!/usr/bin/env python3
"""Discriminator inputs for the native 4:2:2 and 4:2:0 questions
(RESEARCH.md OQ-37 to OQ-41).

Each input is a DSC 1.2 YCbCr PPS (native_422 or native_420) plus a
one-slice payload, built with the Python decoder model in pydsc.py, written
separately from the C decoder. As for the other DSC 1.2 inputs
(make_v12_discriminators.py), the builder constructs the syntax under one
reading of the question and the model decodes the same bits under each
reading: the parse must succeed under each, and the outputs must differ. The
expected outputs are raw YCbCr in the layouts dscdecode writes for NAME.yuv
and the reference model writes for its .yuv files: planar 4:2:0, and UYVY
4:2:2.

Called from make_discriminators.py, which writes manifest.json.
"""
from pydsc import PPS, Builder, clamp
from make_v12_discriminators import BITSAVE_FRAME, COMMON_VARY, emit

# The model's readings of the DSC 1.2 rate-control questions that these
# inputs rely on (all decided by the Phase 4 discriminators).
V12_FRAME = dict(BITSAVE_FRAME, activity_qp='prev2', low_min='min-qp', target_floor='zero',
                 flat_rerun='every', rerun_bitsave='redo', flat_top='at-or-above',
                 bitsave_flat='lagged', bpg_combine='add')


def units(n, triple):
    return [list(triple) for _ in range(n)]


def small(n):
    """Size 2 in every unit."""
    return dict(res=[[1, 0, -1], [0, 1, 0], [0, 0, 1], [-1, 1, 0]][:n])


def zero(n):
    return dict(res=units(n, (0, 0, 0)))


def mpp(n):
    """MPP in every unit; residuals of size 1 fit YCbCr chroma at QP 14."""
    return dict(res=units(n, (0, -1, 0)), mpp=(True,) * n)


def last(n):
    """The last group: residuals whose pixels differ between two QPs even if
    the lower QP shifts the parse by a bit (make_v12_discriminators.LAST);
    the second unit's has size 1, which YCbCr chroma holds at high QPs."""
    return dict(res=[[1, 1, 0], [-1, 0, 0], [0, 0, 0], [1, 0, 0]][:n])


def native_pps(fmt, width, height, **kw):
    return PPS(version=2, rgb=0, native_422=int(fmt == '422'), native_420=int(fmt == '420'),
               width=width, height=height, **kw)


# --- OQ-37 and OQ-38: predActivity in the native modes -------------------------

def activity_input(fmt, question, construct, k_res):
    """48x2 pixels (a 24-pixel container, eight groups a line), 24 bpp of the
    container in 4:2:0 and 30 in 4:2:2 (the first MPP group must not reach
    the increment branch), every range 4-14, so adjustedMaxQp is 15, as in
    oq28_activity_qp. Line 1 starts at group 8; groups 9 to 12 are
    MPP in every unit, which keeps bitSaveMode 2 and raises the QP two a step.
    Group 13 is coded in P-mode at QP 8 with residuals k_res; the predicted
    sizes they give (OQ-23 next) put predActivity (OQ-28: with prev2Qp, the
    QP 8 the group was decoded with) at bitSaveThresh 14 under one reading
    and below it under the other: bitSaveMode is kept, and the step adds 2,
    or reset. Group 14 has zero residuals; group 15, the last, decodes at the
    QP that step generated."""
    n = 4 if fmt == '422' else 3

    def plan(b):
        for _ in range(9):
            b.group(**small(n))
        for _ in range(4):
            b.group(**mpp(n))
        b.group(res=k_res)
        b.group(**zero(n))
        b.group(**last(n))
    pps = native_pps(fmt, 48, 2, bpp16=480 if fmt == '422' else 384, ranges=((4, 14, 0),) * 15,
                     flat_min=15, flat_max=15)
    return pps, plan


def oq37_activity420():
    """Native 4:2:0. Group 13: even luma residuals of size 3 (predicted size
    3), odd luma of size 1 (1), chroma of size 3 (3). luma: 8 + MAX(3, 1) + 3
    = 14 keeps bitSaveMode 2; sum: 8 + MAX(3, 1 + 3) = 12 resets it."""
    pps, plan = activity_input('420', 'activity420', 'luma',
                               [[3, -4, 2], [-1, 0, -1], [3, -4, 2]])
    return emit('oq37_activity420', 'activity420', pps, plan, 'luma',
                vary=['offset_adj', 'activity422', 'ich_window', 'scale_line'], assumes=V12_FRAME)


def oq38_activity422():
    """Native 4:2:2. Group 13: residuals of size 3 in all four units
    (predicted sizes 3, 3, 3, 3; sum 12). sizes: 8 + (12 >> 1) = 14 keeps
    bitSaveMode 2; total: (8 + 12) >> 1 = 10 resets it."""
    pps, plan = activity_input('422', 'activity422', 'sizes', [[3, -4, 2]] * 4)
    return emit('oq38_activity422', 'activity422', pps, plan, 'sizes',
                vary=['activity420', 'offset_adj', 'ich_window', 'scale_line'], assumes=V12_FRAME)


# --- OQ-39: the BP edge test in native 4:2:0 --------------------------------------

def mmap_residuals(b, x, y, targets):
    """Residuals that reconstruct targets[u][j] exactly at QP 0 (qLevel 0)
    with MMAP, or the first-line predictor where unit u has no line above
    (DSC 1.2b §6.4.1). The group must not use BP or MPP."""
    res = []
    for c, target in enumerate(targets):
        mid = 1 << (b.f.depth[c] - 1)
        a = b.cur[x - 1][c] if x else mid
        first = not b.has_above(y, c)

        def blend(k):
            # QP 0: QuantDivisor / 2 = 0, the blend is the unfiltered sample.
            return b.above(k, c)
        cblend = blend(x - 1) if x and not first else mid
        lo = hi = a
        cum, out = 0, []
        for j, t in enumerate(target):
            if first:
                pred = clamp(a + cum, 0, (1 << b.f.depth[c]) - 1)
            else:
                ref = blend(x + j)
                lo, hi = min(lo, ref), max(hi, ref)
                pred = clamp(a + ref - cblend + cum, lo, hi)
            out.append(t - pred)
            cum += t - pred
        res.append(out)
    return res


def oq39_bp420_edge():
    """Native 4:2:0 with block prediction, 36x4 pixels (an 18-pixel
    container, six groups a line), every range pins QP 0. Line 0 has flat
    luma, so no BP vector beats -1 on line 1, and its chroma (Cb) steps from
    100 to 160 between container pixels 15 and 16. Line 1's luma repeats
    every three container pixels (even 100, 110, 120; odd 105, 115, 125:
    no step above 32), so on line 2 the search finds bpVector -3 from hPos
    9 on and bpCount reaches 3 at hPos 15, the last group. The only edge
    near it is the chroma step two lines up, which line 2's chroma is
    predicted from. luma: no edge, MMAP; all: an edge, and the group's luma
    is predicted by BP. Line 3 follows from line 2."""
    luma0 = [100] * 18
    cb0 = [100] * 16 + [160] * 2
    even1 = [(100, 110, 120)[x % 3] for x in range(18)]
    odd1 = [(105, 115, 125)[x % 3] for x in range(18)]
    cr0 = [128] * 18

    def plan(b):
        for x in range(0, 18, 3):
            b.group(res=mmap_residuals(b, x, 0, [luma0[x:x + 3], luma0[x:x + 3], cb0[x:x + 3]]))
        for x in range(0, 18, 3):
            b.group(res=mmap_residuals(b, x, 1, [even1[x:x + 3], odd1[x:x + 3], cr0[x:x + 3]]))
        for _ in range(12):
            b.group(res=[[2, -1, 1], [1, 0, -2], [0, 1, 0]])
    pps = native_pps('420', 36, 4, bpp16=768, ranges=((0, 0, 0),) * 15, flat_min=15, flat_max=15,
                     block_pred=1)
    return emit('oq39_bp420_edge', 'bp420_edge', pps, plan, 'luma', vary=['activity420', 'offset_adj'],
                assumes=dict(V12_FRAME, bp_left='midpoint', bp_edge='window', bp_sad='shift'))


# --- OQ-40: second_line_offset_adj ---------------------------------------------

def oq40_offset_adj():
    """Native 4:2:0, 12x2 pixels (a 6-pixel container, two groups a line),
    48 bpp of the container, rcXformScale 1, second_line_offset_adj 512.
    Range 0 pins QP 0, ranges 1-14 pin QP 4. start adds 512 to the offset at
    the slice start, subtract does not, so rcModelFullness after group 0 is
    512 higher under start; threshold 0 lies between the two. With the range
    lag (OQ-11) that range sets group 3's QP: 0 (subtract) or 4 (start).
    Group 2, the line start, has zero residuals, so group 3's predicted
    sizes are 0 under both."""
    n = 3

    def plan(b):
        b.group(**small(n))
        b.group(**small(n))
        b.group(**zero(n))
        b.group(**last(n))

    def pps_for(threshold):
        return native_pps('420', 12, 2, bpp16=768, initial_offset=2048, scale=8, second_line_adj=512,
                          thresholds=tuple(range(threshold, threshold + 14)),
                          ranges=((0, 0, 0),) + ((4, 4, 0),) * 14, flat_min=15, flat_max=15)
    probe = Builder(pps_for(100), dict(V12_FRAME, offset_adj='subtract'))
    plan(probe)
    m = probe.rc.log[0]['model']
    threshold = (m + 8192) // 64 + 1          # above subtract's value, 512 below start's
    return emit('oq40_offset_adj', 'offset_adj', pps_for(threshold), plan, 'start',
                vary=['activity420', 'line_flat', 'flat_rerun'], assumes=V12_FRAME)


# --- OQ-41: the previous-line ICH window at the slice edges ----------------------

def oq41_ich_window():
    """Native 4:2:2, 12x2 pixels (a 6-pixel container, two groups a line),
    QP 0. Line 0 gives every luma sample a different value. Group 2, the
    first of line 1, is ICH-coded with indices 25, 27 and 29, the first
    three odd-numbered pairs of the line above: pixels (luma samples 0 to 7)
    starts them at sample 0, container (container pixels 0 to 4) at
    sample 1."""
    targets = [[100, 115, 130], [120, 126, 132], [140, 136, 132], [95, 110, 125]]

    def plan(b):
        b.group(res=mmap_residuals(b, 0, 0, targets))
        b.group(res=mmap_residuals(b, 3, 0, [[t + 5 for t in row] for row in targets]))
        b.group(ich=(25, 27, 29))
        b.group(**small(4))
    pps = native_pps('422', 12, 2, bpp16=768, ranges=((0, 0, 0),) * 15, flat_min=15, flat_max=15)
    return emit('oq41_ich_window', 'ich_window', pps, plan, 'container', vary=['activity422'],
                assumes=V12_FRAME)


def oq41b_ich_window():
    """Native 4:2:0, 24x4 pixels (a 12-pixel container, four groups a line),
    QP 0. Lines 0 and 1 use 32 history entries, so the previous-line entries
    start on line 2. Group 11, the last of line 2 (container pixels 9 to 11),
    is ICH-coded with indices 29, 30 and 31: pixels shifts the eight luma
    samples to 16-23, container the container pixels to 7-11, samples 15-22."""
    rows = [[(17 * x + 29 * y) % 60 + 100 for x in range(24)] for y in range(4)]

    def plan(b):
        for y in range(2):
            for x in range(0, 12, 3):
                even = [rows[y][2 * k] for k in range(x, x + 3)]
                odd = [rows[y][2 * k + 1] for k in range(x, x + 3)]
                chroma = [(110, 140)[y] + 7 * k for k in range(x, x + 3)]
                b.group(res=mmap_residuals(b, x, y, [even, odd, chroma]))
        for _ in range(3):
            b.group(**small(3))
        b.group(ich=(29, 30, 31))
        for _ in range(4):
            b.group(**small(3))
    pps = native_pps('420', 24, 4, bpp16=768, ranges=((0, 0, 0),) * 15, flat_min=15, flat_max=15)
    return emit('oq41b_ich_window', 'ich_window', pps, plan, 'container', vary=['activity420', 'offset_adj'],
                assumes=V12_FRAME)


def build_all():
    return [make() for make in (oq37_activity420, oq38_activity422, oq39_bp420_edge, oq40_offset_adj,
                                oq41_ich_window, oq41b_ich_window)]


if __name__ == '__main__':
    import json
    for name, entry in build_all():
        print(name, json.dumps({k: v['qp_schedule'] for k, v in entry['readings'].items()}))
