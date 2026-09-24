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
# The pydsc defaults that differed when the first seven inputs were built
# (60ae6cc), before the model decoded them. Those inputs are rebuilt under
# the same readings, so their files do not change.
PART1_DEFAULTS = {'prefix16': '15', 'bitsave_pred': 'raw', 'bitsave_flat': 'supergroup',
                  'line_flat': 'very'}


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


def emit(name, question, pps, plan, construct, vary=(), note='', separate=None, assumes=None,
         base=None, readings=None, superseded_by=None):
    """Build under reading `construct`, decode under every reading (or those
    listed), write the files and return the manifest entry. `base` sets
    readings the builder and decoders use without recording them."""
    fixed = dict(base or {}, **(assumes or {}))
    b = Builder(pps, dict(fixed, **{question: construct}))
    plan(b)
    assert len(b.units) == (pps.slice_width + 2) // 3 * pps.slice_height, (name, 'plan size')
    payload, used = b.payload()
    if pps.bpc == 16:
        # The refill threshold of the luma substream at 16 bpc is not stated
        # (4 * bpc + 4 = 68, or the 64-bit mux word): the input must not
        # depend on it.
        assert mux_order(b.units, b.f, (68, 64, 64)) == mux_order(b.units, b.f, (64, 64, 64)), name
    outputs, logs = {}, {}
    for value in readings or READINGS[question]:
        d = Decoder(pps, dict(fixed, **{question: value})).decode(payload)
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
    if superseded_by:
        entry['superseded_by'] = superseded_by
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
                vary=['chroma_qlevel', 'bitsave_ich', 'bitsave_flat', 'line_flat'], base=PART1_DEFAULTS)


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
                vary=['prefix16', 'bpg_combine'], base=PART1_DEFAULTS)


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
    return emit('oq21_prefix16', 'prefix16', pps, plan, '15', vary=['chroma_qlevel'],
                base=PART1_DEFAULTS)


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
                vary=['bitsave_ich', 'bitsave_pred', 'bitsave_flat'], base=PART1_DEFAULTS)


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


def bitsave_pps(width, qp=8, height=2, **kw):
    return PPS(version=2, width=width, height=height, bpp16=384, ranges=((qp, qp, 0),) * 15, **kw)


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
                vary=['bitsave_pred', 'bitsave_flat', 'line_flat', 'bpg_combine'],
                base=PART1_DEFAULTS)


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
                vary=['bitsave_flat', 'line_flat', 'bpg_combine'], assumes={'bitsave_ich': 'not'},
                base=PART1_DEFAULTS)


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
                assumes={'bitsave_ich': 'not'}, base=PART1_DEFAULTS,
                readings=('supergroup', 'group'), superseded_by='oq24b_bitsave_flat')


# --- OQ-24 again: which groups the flatness test covers ----------------------
#
# oq24_bitsave_flat's two predictions both failed (PROGRESS.md, Phase 4); the
# model's output fitted two further readings, received and carrier. With
# flatness_min_qp = flatness_max_qp = 8 a flag is sendable at every group of
# these inputs, and a signaled flat group changes nothing: the group before
# it decodes at QP 8, range 14's maximum (OQ-18). Supergroups are groups 1-4,
# 5-8, 9-12; the flag for one is sent in its group 3 before (7 mod 4 = 3),
# the type and position in the next group. For the flag sent in group 7 the
# readings cover: supergroup 9-12; received 7-10; carrier 7 and 8; group the
# flat group. Each input ends with two MPP groups, a zero group and the last
# group; the last decodes at 9 when neither MPP group is covered by a flag of
# 1 (bitSaveMode 2), otherwise at 8.

def oq24b_bitsave_flat():
    """15x3: five groups a line. Group 7 sends flag 1 for supergroup 9-12,
    group 8 type 0 and position 2 (flat group 11), group 11 flag 0 for
    supergroup 13-16. Groups 11 and 12 are MPP, 13 zeros, 14 the last.
    received and carrier: 11 and 12 see group 11's flag 0, bitSaveMode 2,
    group 14 at 9. supergroup: both lie in 9-12, flag 1, group 14 at 8.
    group: 11 is the flat group, group 14 at 8."""
    def plan(b):
        for _ in range(7):
            b.group(**SMALL)
        b.group(flat=(1, 0, 2), **SMALL)                   # group 7: flag 1
        b.group(flat=(1, 0, 2), **SMALL)                   # group 8: type 0, position 2
        b.group(**SMALL)
        b.group(**SMALL)
        b.group(flat=(0, 0, 0), **MPP3)                    # group 11: flag 0
        b.group(**MPP3)
        b.group(res=ZERO)
        b.group(**LAST)         # group 14
    return emit('oq24b_bitsave_flat', 'bitsave_flat',
                bitsave_pps(15, height=3, flat_min=8, flat_max=8), plan, 'received',
                vary=['bitsave_pred', 'line_flat', 'bpg_combine'], assumes={'bitsave_ich': 'not'},
                readings=('supergroup', 'group', 'received', 'carrier'))


def oq24c_bitsave_flat():
    """9x3: three groups a line. Group 3 sends flag 1 for supergroup 5-8,
    group 4 type 0 and position 3 (flat group 8, the last). Groups 5 and 6
    are MPP, 7 zeros, 8 the last. received: 5 and 6 see group 3's flag 1,
    group 8 at 8; supergroup: both lie in 5-8, group 8 at 8. carrier: 5 and
    6 carry no flatness bits, bitSaveMode 2, group 8 at 9; group: neither
    is the flat group, group 8 at 9."""
    def plan(b):
        for _ in range(3):
            b.group(**SMALL)
        b.group(flat=(1, 0, 3), **SMALL)                   # group 3: flag 1
        b.group(flat=(1, 0, 3), **SMALL)                   # group 4: type 0, position 3
        b.group(**MPP3)
        b.group(**MPP3)
        b.group(res=ZERO)
        b.group(**LAST)         # group 8
    return emit('oq24c_bitsave_flat', 'bitsave_flat',
                bitsave_pps(9, height=3, flat_min=8, flat_max=8), plan, 'received',
                vary=['bitsave_pred', 'line_flat', 'bpg_combine'], assumes={'bitsave_ich': 'not'},
                readings=('supergroup', 'group', 'received', 'carrier'))


# --- OQ-24, third round: the ends of the window ------------------------------
#
# On the three inputs above the model's outputs fit span (the flag's group to
# the last group of its supergroup: 7-12 for the flag sent in group 7) and
# lagged (the flag as it was before the group: 8-11), and the windows 7-11
# and 8-12. Two inputs test the two ends, each with one MPP pair whose other
# group no reading covers: is the flag's own group covered (oq24d), is the
# supergroup's last group covered (oq24e).

def oq24d_bitsave_flat():
    """15x2: five groups a line. Group 7 sends flag 1 for supergroup 9-12,
    group 8 type 0 and position 3 (flat group 12, beyond the slice); group
    3 sent flag 0. Groups 6 and 7 are MPP, 8 zeros, 9 the last. Group 7 is
    covered under received, carrier and span (group 9 at 8), not under
    supergroup, group and lagged (group 9 at 9)."""
    def plan(b):
        for _ in range(6):
            b.group(**SMALL)
        b.group(**MPP3)                                    # group 6
        b.group(flat=(1, 0, 3), **MPP3)                    # group 7: flag 1
        b.group(flat=(1, 0, 3), res=ZERO)                  # group 8: type 0, position 3
        b.group(**LAST)         # group 9
    return emit('oq24d_bitsave_flat', 'bitsave_flat',
                bitsave_pps(15, flat_min=8, flat_max=8), plan, 'span',
                vary=['bitsave_pred', 'line_flat', 'bpg_combine'], assumes={'bitsave_ich': 'not'})


def oq24e_bitsave_flat():
    """24x2: eight groups a line. Group 7 sends flag 1 for supergroup 9-12,
    group 8 type 0 and position 0 (flat group 9), group 11 flag 0. Groups 12
    and 13 are MPP, 14 zeros, 15 the last. Group 12 is covered under
    supergroup and span (group 15 at 8), not under group, received, carrier
    and lagged (group 15 at 9)."""
    def plan(b):
        for _ in range(7):
            b.group(**SMALL)
        b.group(flat=(1, 0, 0), **SMALL)                   # group 7: flag 1
        b.group(flat=(1, 0, 0), **SMALL)                   # group 8: type 0, position 0
        b.group(**SMALL)
        b.group(**SMALL)
        b.group(flat=(0, 0, 0), **SMALL)                   # group 11: flag 0
        b.group(**MPP3)                                    # group 12
        b.group(**MPP3)
        b.group(res=ZERO)
        b.group(**LAST)         # group 15
    return emit('oq24e_bitsave_flat', 'bitsave_flat',
                bitsave_pps(24, flat_min=8, flat_max=8), plan, 'span',
                vary=['bitsave_pred', 'line_flat', 'bpg_combine'], assumes={'bitsave_ich': 'not'})


def build_all():
    return [make() for make in (oq7_bpg_combine, oq20_chroma_qlevel, oq21_prefix16,
                                oq22_bitsave_ich, oq23_bitsave_pred_next, oq24_bitsave_flat,
                                oq25_line_flat, oq24b_bitsave_flat, oq24c_bitsave_flat,
                                oq24d_bitsave_flat, oq24e_bitsave_flat)]


if __name__ == '__main__':
    import json
    for name, entry in build_all():
        print(name, json.dumps({k: v['qp_schedule'] for k, v in entry['readings'].items()}))
