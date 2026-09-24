#!/usr/bin/env python3
"""Discriminator inputs for the DSC 1.2 questions (RESEARCH.md OQ-7, OQ-20
to OQ-25).

Each input is a DSC 1.2 PPS plus a one-slice payload, built with the Python
decoder model in pydsc.py, which is written separately from the C decoder.
A builder constructs the syntax under one reading of the question; the model
then decodes the same bits under every reading of the question. The parse
must succeed under each, and the outputs must differ between readings (a
question with three readings may need two inputs, each separating one pair;
readings an input does not separate share an expected image: OQ-23's input
separates next from raw and adjusted, and no input built under the RGB
4:4:4 constraints separates raw from adjusted, see RESEARCH.md).

Every input is designed so that every other reading switch, and the maximum
syntax element size at 16 bpc, leaves the output unchanged; the C decoder
checks the first under every combination in tests/test_discriminators.py.

Called from make_discriminators.py, which writes manifest.json.
"""
import hashlib
from pathlib import Path

from pydsc import PPS, Builder, Decoder, READINGS

OUT = Path(__file__).parent / 'discriminators'
ZERO = [[0, 0, 0]] * 3
# Switches every DSC 1.2 input is decoded under (all combinations) by
# tests/test_discriminators.py, besides its own question.
COMMON_VARY = ['flat_restart', 'threshold_eq', 'delay_offset', 'delay_partial']
MODEL_TIMING = {'incr_order': 'swapped', 'rc_pipeline': 'range-lag', 'scale_dec': 'from-group-0',
                'partial_target': 'pixels', 'very_flat': 'previous-qp', 'partial_padding': 'accept',
                'flat_max_qp': 'previous'}


def mux_order(units, fmt, max_se):
    """The substream of each mux word, for a given set of refill thresholds."""
    full, order = [0, 0, 0], []
    for u in units:
        for c in range(3):
            if full[c] < max_se[c]:
                order.append(c)
                full[c] += fmt.mux
        for c in range(3):
            full[c] -= len(u[c])
    return order


def emit(name, question, pps, plan, construct, vary=(), note='', separate=None, assumes=None):
    """Build under reading `construct`, decode under every reading, write the
    files and return the manifest entry."""
    b = Builder(pps, dict(assumes or {}, **{question: construct}))
    plan(b)
    assert len(b.units) == (pps.slice_width + 2) // 3 * pps.slice_height, (name, 'plan size')
    payload, used = b.payload()
    if pps.bpc == 16:
        # The refill threshold of the luma substream at 16 bpc is not stated
        # (4 * bpc + 4 = 68, or the 64-bit mux word): the input must not
        # depend on it.
        assert mux_order(b.units, b.f, (68, 64, 64)) == mux_order(b.units, b.f, (64, 64, 64)), name
    outputs, logs = {}, {}
    for value in READINGS[question]:
        d = Decoder(pps, dict(assumes or {}, **{question: value})).decode(payload)
        outputs[value] = d.ppm()
        logs[value] = d.rc.log
    distinct = len(set(outputs.values()))
    assert distinct >= 2, (name, 'readings agree')
    if separate:
        assert outputs[separate[0]] != outputs[separate[1]], (name, separate, 'agree')
    (OUT / f'{name}.pps').write_bytes(pps.bytes())
    (OUT / f'{name}.bin').write_bytes(payload)
    (OUT / f'{name}.syntax.txt').write_text(''.join(' '.join(u) + '\n' for u in b.units))
    entry = dict(question=question, vary=[question] + list(vary) + COMMON_VARY,
                 assumes=dict(MODEL_TIMING, **(assumes or {})), width=pps.width, height=pps.height, bpc=pps.bpc,
                 dsc_version='1.2', mux_bits=used, payload_bits=8 * len(payload),
                 pps_sha256=hashlib.sha256(pps.bytes()).hexdigest(),
                 payload_sha256=hashlib.sha256(payload).hexdigest(), readings={})
    if note:
        entry['note'] = note
    for value, ppm in outputs.items():
        (OUT / f'{name}.{value}.expected.ppm').write_bytes(ppm)
        entry['readings'][value] = dict(expected_sha256=hashlib.sha256(ppm).hexdigest(),
                                        qp_schedule=[r['qp'] for r in logs[value]])
    return name, entry


def solve_offset(pps_for, plan, step, threshold, below=True):
    """initial_offset such that rcModelFullness after group `step` is on the
    other side of `threshold` (PPS units) from every group before it, midway
    in the gap; with the range lag, the step after group step + 1 is the
    first to use the range beyond the threshold. pps_for(initial_offset)
    gives the PPS; every range must pin the QP while solving, so the offset
    cannot change the syntax."""
    b = Builder(pps_for(2048))
    plan(b)
    m = [r['model'] for r in b.rc.log]
    t = threshold * 64 - pps_for(2048).model_size
    if below:   # m decreasing: m[step-1] > t >= m[step] after the shift
        lo, hi = t - m[step - 1], t - m[step]
    else:       # m increasing: m[step-1] <= t < m[step]
        lo, hi = t - m[step], t - m[step - 1]
    assert hi - lo >= 2, ('no room between the steps', lo, hi)
    return 2048 + (lo + hi) // 2 + 1


# --- OQ-7: rcXformBpgOffset, "+=" or "=" -----------------------------------

def oq7_bpg_combine():
    """One line of ten groups, 20 bpp, first_line_bpg_offset 15.

    rcTgtBitsGroup on the first line is 60 + 15 = 75 under add and 60 under
    replace (the second-line terms are zero outside native 4:2:0). Ranges
    6 to 14 pin QP 0; ranges 0 to 5 allow 0 to 8. rcModelFullness falls from
    group to group (the offset falls 75 bits a group, fewer bits arrive);
    the initial offset makes it cross threshold 5 after group 6, so only the
    step after group 7 uses an unpinned range (range lag). Group 7 codes 79
    bits with the buffer above 192 bits: the increment branch under both
    readings, from curQp 0, by (79 - 75) >> 1 = 2 or MIN(8, (79 - 60) >> 1)
    = 8. That QP decodes group 9, the last, whose luma residual 1 adds 1
    at QP 2 (qLevelY 0) or 8 at QP 8 (qLevelY 3). Group 8 has zero residuals,
    so group 9 parses the same at both QPs.
    """
    def plan(b):
        for _ in range(7):
            b.group(res=[[3, -2, 1], [1, 0, -1], [0, 1, 0]])
        b.group(res=[[40, -40, 33], [20, -20, 17], [40, -40, 33]])
        b.group(res=ZERO)
        b.group(res=[[1, 0, 0], ZERO[0], ZERO[0]])

    def pps(offset, ranges=((0, 0, 0),) * 15):
        return PPS(version=2, width=30, height=1, bpp16=320, first_line_bpg=15,
                   initial_offset=offset,
                   thresholds=(10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 125, 126),
                   ranges=ranges)
    offset = solve_offset(pps, plan, 6, 60)
    return emit('oq7_bpg_combine', 'bpg_combine',
                pps(offset, ((0, 8, 0),) * 6 + ((0, 0, 0),) * 9), plan, 'add',
                vary=['chroma_qlevel', 'bitsave_ich', 'bitsave_flat', 'line_flat'])


# --- OQ-20: qLevelC at 16 bpc RGB --------------------------------------------

def oq20_chroma_qlevel():
    """16 bpc RGB, one line of eight groups; every range pins QP 3. Groups 0
    and 1 decode at QP 0, the rest at QP 3: qLevelY 1 and qLevelC 2 (Table
    6-3), or 1 with DSC 1.2's equal-depth adjustment (§6.8.6). Group 7, the
    last, codes Co residuals (-1, 0, 0): -4 or -2 after inverse quantization.
    Group 6 has zero residuals, so group 7's prefix parses the same under both
    readings."""
    def plan(b):
        for _ in range(7):
            b.group(res=ZERO)
        b.group(res=[[1, 0, 0], [-1, 0, 0], [0, 0, 0]])
    pps = PPS(version=2, bpc=16, line_buf=16, width=24, height=1, bpp16=384,
              ranges=((3, 3, 0),) * 15, flat_min=31, flat_max=31, limit0=31, limit1=31)
    return emit('oq20_chroma_qlevel', 'chroma_qlevel', pps, plan, 'equal-depth',
                vary=['prefix16', 'bpg_combine'])


# --- OQ-21: the 16 bpc luma prefix limit at QP 0 -----------------------------

def oq21_prefix16():
    """16 bpc RGB, one line of eight groups, every range pinning QP 0. The
    last group's luma prefix is thirteen zeros. Under the 15-bit limit that
    is a size of 13 followed by "1" and three 13-bit residuals; under the
    13-bit limit it is the MPP escape, and the decoder reads three 16-bit
    residuals starting at that "1". Earlier groups code zero residuals, one
    prefix bit each."""
    def plan(b):
        for _ in range(7):
            b.group(res=ZERO)
        b.group(res=[[3000, -2000, 1000], ZERO[0], ZERO[0]], widths=(13, None, None))
    pps = PPS(version=2, bpc=16, line_buf=16, width=24, height=1, bpp16=384,
              flat_min=31, flat_max=31, limit0=31, limit1=31)
    return emit('oq21_prefix16', 'prefix16', pps, plan, '15', vary=['chroma_qlevel'])


# --- OQ-25: the first group of a line, very flat or demoted -------------------

def oq25_line_flat():
    """3x2, 8 bpc: one group per line. Both groups of a slice's first two
    groups decode at QP 0 (§6.8). Group 1 starts line 1, so DSC 1.2 adjusts
    it as very flat (§6.8.5.2): veryFlatQp 1 under 'very'; under 'signaled'
    the QP that decoded the group before (0) is below somewhatFlatQpThresh
    (7), which demotes it to somewhat flat, MAX(0 - 4, 0) = 0, no change.
    Group 1's Co residual -1 becomes -2 at QP 1 (qLevelC 1) or -1 at QP 0."""
    def plan(b):
        b.group(res=ZERO)
        b.group(res=[[0, 0, 0], [-1, 0, 0], [0, 0, 0]])
    pps = PPS(version=2, width=3, height=2, bpp16=384, ranges=((0, 8, 0),) * 15)
    return emit('oq25_line_flat', 'line_flat', pps, plan, 'very',
                vary=['bitsave_ich', 'bitsave_pred', 'bitsave_flat'])


# --- OQ-22 to OQ-24: bitSaveMode ----------------------------------------------
#
# Common frame: 8 bpc, 24 bpp, every range pins QP Q with range 14's maximum
# also Q, so the DSC 1.2 line-start adjustment never applies (the QP before
# the line start equals range 14's maximum, §6.8.5.2 and OQ-18). Two MPP
# groups in a row on the second line set mppState to 2 and bitSaveMode to 2
# (DSC 1.2b §6.8.4); the next step then returns prevQp + 1, clamped to
# adjustedMaxQp = Q + 1.
MPP3 = dict(res=[[1, -1, 0], [1, -1, 0], [1, -1, 0]], mpp=(True, True, True))
SMALL = dict(res=[[1, 0, -1], [0, 1, 0], [0, 0, 1]])
# The last group: residuals whose decoded pixels differ between the two QPs
# even when the lower QP shifts the parse by one bit.
LAST = dict(res=[[1, 1, 0], [1, 0, 0], [0, 0, 0]])


def bitsave_pps(width, qp=8, **kw):
    return PPS(version=2, width=width, height=2, bpp16=384, ranges=((qp, qp, 0),) * 15, **kw)


def oq22_bitsave_ich():
    """15x2: five groups a line. Line 1: group 5 small (line start), groups
    6 and 7 MPP in all units, group 8 zeros, group 9 last. DSC 1.2b
    (!ichSelected): the step after group 7 finds mppState 2 and sets
    bitSaveMode 2, so group 9 decodes at MIN(8 + 1, 9) = 9. DSC 1.2a as
    printed (ichSelected): an MPP group is not ICH, the step resets
    bitSaveMode, and group 9 decodes at 8. Group 9's luma residual 1 adds 16
    or 8 (qLevelY 4 or 3)."""
    def plan(b):
        for _ in range(5):
            b.group(**SMALL)
        b.group(**SMALL)
        b.group(**MPP3)
        b.group(**MPP3)
        b.group(res=ZERO)
        b.group(**LAST)
    return emit('oq22_bitsave_ich', 'bitsave_ich', bitsave_pps(15), plan, 'not',
                vary=['bitsave_pred', 'bitsave_flat', 'line_flat', 'bpg_combine'])


def oq23_bitsave_pred_next():
    """18x2: six groups a line. Line 1: group 6 small, groups 7 and 8 MPP
    (bitSaveMode 2 after group 8, so group 10 decodes at 9), group 9 zeros,
    group 10 zeros, group 11 last. predActivity after group 9 is prevQp 9
    plus the predicted luma size plus the larger chroma one: from group 8's
    MPP sizes as coded (raw) 9 + 5 + 4 = 18, clamped to maxSize - 1
    (adjusted) 9 + 4 + 3 = 16, both at least bitSaveThresh 15, so bitSaveMode
    stays 2 and group 11 decodes at 9; from group 9's own zero residuals
    (next) 9 + 0 + 0 = 9, which resets it, and the zero-residual branch
    gives CLAMP(9 - 1, 4, 8) = 8."""
    def plan(b):
        for _ in range(6):
            b.group(**SMALL)
        b.group(**SMALL)
        b.group(**MPP3)
        b.group(**MPP3)
        b.group(res=ZERO)
        b.group(res=ZERO)
        b.group(**LAST)
    # Reaching bitSaveMode 2 through MPP groups is DSC 1.2b's reading of
    # OQ-22; this input assumes it.
    return emit('oq23_bitsave_pred_next', 'bitsave_pred', bitsave_pps(18), plan, 'raw',
                vary=['bitsave_flat', 'line_flat', 'bpg_combine'], assumes={'bitsave_ich': 'not'})


def oq24_bitsave_flat():
    """18x2: six groups a line; line 1 starts at group 6. Groups 7 and 8 are
    MPP (bitSaveMode 2 after group 8). Group 7 (7 mod 4 = 3) sends flatness
    flag 1 for the supergroup of groups 9 to 12, and group 8 its type and
    position 3: group 12, beyond the slice, is the flat group. Group 9, the
    first of that supergroup, is MPP. Under 'supergroup' flatness is
    signaled for the supergroup holding group 9, so the step after it resets
    bitSaveMode; under 'group' group 9 is not the signaled group, and the MPP
    group keeps bitSaveMode 2. Group 11, the last, decodes at 8 or 9.
    Flatness flags are sendable only at QP 8 (flatness_min_qp = max = 8)."""
    def plan(b):
        for _ in range(6):
            b.group(**SMALL)
        b.group(**SMALL)                                   # group 6: line start
        b.group(flat=(1, 0, 3), **MPP3)                    # group 7: flag 1
        b.group(flat=(1, 0, 3), **MPP3)                    # group 8: type 0, position 3
        b.group(**MPP3)                                    # group 9
        b.group(res=ZERO)                                  # group 10
        b.group(**LAST)         # group 11
    return emit('oq24_bitsave_flat', 'bitsave_flat', bitsave_pps(18, flat_min=8, flat_max=8), plan,
                'group', vary=['bitsave_pred', 'line_flat', 'bpg_combine'],
                assumes={'bitsave_ich': 'not'})


def build_all():
    return [make() for make in (oq7_bpg_combine, oq20_chroma_qlevel, oq21_prefix16,
                                oq22_bitsave_ich, oq23_bitsave_pred_next, oq24_bitsave_flat,
                                oq25_line_flat)]


if __name__ == '__main__':
    import json
    for name, entry in build_all():
        print(name, json.dumps({k: v['qp_schedule'] for k, v in entry['readings'].items()}))
