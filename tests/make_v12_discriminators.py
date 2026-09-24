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

from pydsc import PPS, Builder, Decoder, READINGS, TEXT

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
                  'line_flat': 'very', **TEXT}
# Every input built before OQ-26 to OQ-34 existed followed the text there.


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
         base=None, readings=None, superseded_by=None, common=None):
    """Build under reading `construct`, decode under every reading (or those
    listed), write the files and return the manifest entry. `base` sets
    readings the builder and decoders use without recording them."""
    fixed = dict(base or {}, **(assumes or {}))
    b = Builder(pps, dict(fixed, **{question: construct}))
    plan(b)
    assert len(b.units) == (pps.coded_width + 2) // 3 * pps.slice_height, (name, 'plan size')
    payload, used = b.payload()
    if pps.bpc == 16 and question != 'mux16':
        # The refill threshold of the luma substream at 16 bpc is OQ-33
        # (4 * bpc + 4 = 68, or the 64-bit mux word): only its own input may
        # depend on it.
        assert mux_order(b.units, b.f, (68, 64, 64)) == mux_order(b.units, b.f, (64, 64, 64)), name
    outputs, logs = {}, {}
    for value in readings or READINGS[question]:
        d = Decoder(pps, dict(fixed, **{question: value})).decode(payload)
        outputs[value] = d.output()
        logs[value] = d.rc.log
    distinct = len(set(outputs.values()))
    assert distinct >= 2, (name, 'readings agree')
    if separate:
        assert outputs[separate[0]] != outputs[separate[1]], (name, separate, 'agree')
    (OUT / f'{name}.pps').write_bytes(pps.bytes())
    (OUT / f'{name}.bin').write_bytes(payload)
    (OUT / f'{name}.syntax.txt').write_text(''.join(' '.join(u) + '\n' for u in b.units))
    entry = dict(question=question, vary=[question] + list(vary) + (COMMON_VARY if common is None else common),
                 assumes=dict(MODEL_TIMING, **(assumes or {})), width=pps.width, height=pps.height, bpc=pps.bpc,
                 dsc_version=f'1.{pps.version}', mux_bits=used, payload_bits=8 * len(payload),
                 pps_sha256=hashlib.sha256(pps.bytes()).hexdigest(),
                 payload_sha256=hashlib.sha256(payload).hexdigest(), readings={})
    if note:
        entry['note'] = note
    if superseded_by:
        entry['superseded_by'] = superseded_by
    # YCbCr inputs: the expected output is raw YCbCr (dscdecode's .yuv).
    ext = 'ppm' if pps.rgb else 'yuv'
    if not pps.rgb:
        entry['output'] = 'yuv'
        entry['format'] = ('native_422' if pps.native_422 else 'native_420' if pps.native_420 else
                           'simple_422' if pps.simple_422 else 'ycbcr_444')
    for value, image in outputs.items():
        (OUT / f'{name}.{value}.expected.{ext}').write_bytes(image)
        entry['readings'][value] = dict(expected_sha256=hashlib.sha256(image).hexdigest(),
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
                readings=('supergroup', 'group', 'received', 'carrier'), base=TEXT)


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
                readings=('supergroup', 'group', 'received', 'carrier'), base=TEXT)


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
                vary=['bitsave_pred', 'line_flat', 'bpg_combine'], assumes={'bitsave_ich': 'not'},
                base=TEXT)


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
                vary=['bitsave_pred', 'line_flat', 'bpg_combine'], assumes={'bitsave_ich': 'not'},
                base=TEXT)


# --- OQ-26 to OQ-31: the short-term RC where the model departs from the text -
#
# Each input ends with a group whose QP one step decides; the text and the
# model's reading give it different QPs, and every group before it has the
# same QP under both. The frames keep the steps before the decisive one
# ordinary: flatness_min_qp = flatness_max_qp = 15 sends no flatness flag
# unless stated, and a single line leaves bitSaveMode and line starts out.
BIG = dict(res=[[7, -8, 5], [7, -8, 5], [7, -8, 5]])      # size 4 in every unit
# Readings the bit-saving inputs depend on: the model's (OQ-22, OQ-23, OQ-25)
# and the observed ones of OQ-27 and OQ-29.
BITSAVE_FRAME = {'decrement_test': 'size', 'bitsave_step': '2', 'bitsave_pred': 'next',
                 'line_flat': 'signaled', 'bitsave_ich': 'not'}
TINY = dict(res=[[-1, 0, 0], [0, -1, 0], [0, 0, -1]])     # size 1 in every unit
LUMA1 = dict(res=[[1, 0, 0], [0, 0, 0], [0, 0, 0]])


def oq26_low_min():
    """45x1, 24 bpp, every range 6-10. Twelve small groups bring the buffer
    past 192 bits at QP 6; group 12 has zero residuals: prevQp - 1 = 5,
    clamped to lowMinQp = MAX(maxQp - 4, 0) = 6 (max-qp) or MAX(minQp - 4,
    0) = 2 (min-qp). Group 14, the last, decodes at 6 or 5."""
    def plan(b):
        for _ in range(12):
            b.group(**SMALL)
        b.group(res=ZERO)
        b.group(res=ZERO)
        b.group(**LAST)
    pps = PPS(version=2, width=45, height=1, bpp16=384, ranges=((6, 10, 0),) * 15, flat_min=15, flat_max=15)
    return emit('oq26_low_min', 'low_min', pps, plan, 'min-qp', vary=['decrement_test', 'target_floor'])


def oq27_decrement_test():
    """48x1, 12 bpp (rcTgtBitsGroup 36, tgtMinusOffset 33), every range
    2-10. After twelve small groups, group 12 codes large residuals (size 4)
    and group 13 small ones (size 1) at the predicted size 4: codedGroupSize
    39, rcSizeGroup 12. both: 39 is not below 33, and not above
    tgtPlusOffset 39 either, so QP stays 8; size: rcSizeGroup 12 < 33, QP 7.
    Group 15, the last, decodes at 8 or 7."""
    def plan(b):
        for _ in range(12):
            b.group(**SMALL)
        b.group(**BIG)
        b.group(**TINY)
        b.group(res=ZERO)
        b.group(**LAST)
    pps = PPS(version=2, width=48, height=1, bpp16=192, ranges=((2, 10, 0),) * 15, flat_min=15, flat_max=15)
    return emit('oq27_decrement_test', 'decrement_test', pps, plan, 'size', vary=['low_min', 'target_floor'])


def oq29_bitsave_step():
    """15x2, 24 bpp, every range 4-12 (adjustedMaxQp 13). Line 1: a small
    group, two MPP groups (bitSaveMode 2 after the second), a zero group and
    the last. The step after the second MPP group gives prevQp 4 + 1 = 5 or
    + 2 = 6, below adjustedMaxQp, so group 9 decodes at 5 or 6."""
    def plan(b):
        for _ in range(6):
            b.group(**SMALL)
        b.group(**MPP3)
        b.group(**MPP3)
        b.group(res=ZERO)
        b.group(**LAST)
    pps = PPS(version=2, width=15, height=2, bpp16=384, ranges=((4, 12, 0),) * 15, flat_min=15, flat_max=15)
    return emit('oq29_bitsave_step', 'bitsave_step', pps, plan, '2', vary=['activity_qp', 'rerun_bitsave'],
                assumes={k: v for k, v in BITSAVE_FRAME.items() if k != 'bitsave_step'})


def oq30_target_floor():
    """54x1, 8 bpp, every range 0-15 with range_bpg_offset -32, so
    rcTgtBitsGroup is 24 - 32 = -8. The slice lies within the initial
    transmission delay, so bufferFullness only grows: nine zero groups and
    seven small ones bring it to 198 bits at group 15, the first step not
    held at minQp by bufferFullness < 192. Its increment is (24 + 8) >> 1 =
    16 (none) or (24 - 0) >> 1 = 12 (zero), from QP 0: group 17, the last,
    decodes at MIN(15, 16) = 15 or 12."""
    def plan(b):
        for _ in range(9):
            b.group(res=ZERO)
        for _ in range(7):
            b.group(**SMALL)
        b.group(res=ZERO)
        b.group(**LUMA1)
    pps = PPS(version=2, width=54, height=1, bpp16=128, ranges=((0, 15, 32),) * 15, flat_min=15, flat_max=15)
    return emit('oq30_target_floor', 'target_floor', pps, plan, 'zero', vary=['low_min', 'decrement_test'])


def oq31_flat_rerun():
    """48x1, 12 bpp, every range 0-4, flatness flags sendable at every QP.
    A large group raises the QP, zero groups lower it by one a step, so
    group 13 (large) decodes at 1 and group 14 at 0. Group 11 sends flag 1,
    group 12 type 0 and position 1: group 14 is somewhat flat, MAX(0 - 4,
    0) = 0, no change. changed: nothing is re-run, and the step after group
    13 (prev2Qp 1, curQp 0 below it) increments, group 15 at 4. every: the
    step is re-run with prev2Qp adjusted to MAX(1 - 4, 0) = 0; curQp equals
    it and the edge test fails (rcSizeGroup 39 against 3), group 15 at 0."""
    flats = {11: (1, 0, 1), 12: (1, 0, 1)}

    def plan(b):
        for i, c in enumerate('SSSSSSSSBZZZZBZL'):
            b.group(flat=flats.get(i), **{'S': SMALL, 'B': BIG, 'Z': dict(res=ZERO), 'L': LAST}[c])
    pps = PPS(version=2, width=48, height=1, bpp16=192, ranges=((0, 4, 0),) * 15, flat_min=0, flat_max=15)
    return emit('oq31_flat_rerun', 'flat_rerun', pps, plan, 'every', vary=['rerun_bitsave', 'activity_qp'],
                assumes={'flat_restart': 'in-flight'}, common=COMMON_VARY[1:])


def oq28_activity_qp():
    """24x2, 24 bpp, every range 4-14 (adjustedMaxQp 15). Line 1: after the
    line start, four MPP groups keep bitSaveMode 2 and raise the QP by 2 a
    step. Group 13 is not MPP; its predicted sizes (luma 3, chroma 2) give
    predActivity 10 + 5 = 15 with prevQp (bitSaveMode kept, the step adds
    2) or 8 + 5 = 13 with prev2Qp, the QP it was decoded with (reset: the
    step keeps prevQp 10, minus 1 for the zero group after it). Group 15,
    the last, decodes at 12 (prev) or 9 (prev2)."""
    def plan(b):
        for _ in range(9):
            b.group(**SMALL)
        for _ in range(4):
            b.group(**MPP3)
        b.group(res=[[3, -4, 2], [1, -2, 1], [1, -2, 1]])
        b.group(res=ZERO)
        b.group(**LAST)
    pps = PPS(version=2, width=24, height=2, bpp16=384, ranges=((4, 14, 0),) * 15, flat_min=15, flat_max=15)
    return emit('oq28_activity_qp', 'activity_qp', pps, plan, 'prev2', vary=['rerun_bitsave', 'target_floor'],
                assumes=BITSAVE_FRAME)


def oq32_rerun_bitsave():
    """9x4, 16 bpp, three groups a line, ranges 6-11 (range 14: 6-12).
    Groups 6 and 7 are MPP (bitSaveMode 2); group 8, the last of line 2,
    decodes at QP 11 with predicted sizes 2, 2: predActivity 11 + 4 = 15
    keeps bitSaveMode 2. Line 3 starts at group 9, adjusted as very flat to
    QP 1, and the step after group 8 is re-run with prev2Qp adjusted to 1.
    keep: bitSaveMode stays 2; redo: 1 + 4 = 5 resets it. Group 9 is MPP:
    mppState goes to 2 again (keep) or only to 1 (redo), so the step after
    it adds 2 or does not. Group 11, the last, decodes at 8 or 6."""
    def plan(b):
        for c in 'SSSSZBMMkMZL':
            b.group(**{'S': SMALL, 'Z': dict(res=ZERO), 'B': BIG, 'M': MPP3, 'L': LAST,
                       'k': dict(res=[[1, -2, 1], [1, -2, 1], [1, -2, 1]])}[c])
    pps = PPS(version=2, width=9, height=4, bpp16=256, ranges=((6, 11, 0),) * 14 + ((6, 12, 0),),
              flat_min=15, flat_max=15)
    return emit('oq32_rerun_bitsave', 'rerun_bitsave', pps, plan, 'redo', vary=['activity_qp', 'flat_rerun'],
                assumes=dict(BITSAVE_FRAME, flat_restart='in-flight'), common=COMMON_VARY[1:])


# 16 bpc codings: MPP luma with 15-bit residuals at QP 3, MPP chroma, both.
P16 = dict(res=[[-10000, -7000, 3000], [0, 0, 0], [0, 0, 0]], mpp=(True, False, False))
Q16 = dict(res=[[-10000, -7000, 3000], [-9000, 5000, 100], [8000, -6000, 200]], mpp=(True, True, True))
R16 = dict(res=[[0, 0, 0], [-9000, 5000, 100], [8000, -6000, 200]], mpp=(False, True, True))


def pps16(width, ranges, **kw):
    return PPS(version=2, bpc=16, line_buf=16, width=width, height=1, bpp16=384, ranges=(ranges,) * 15,
               flat_min=31, flat_max=31, limit0=31, limit1=31, **kw)


def oq33_mux16():
    """16 bpc, 24x1, every range pins QP 3. Chroma MPP in group 3 and luma
    MPP in group 4 leave the luma funnel between 64 and 67 bits when the
    chroma funnels are low: with a threshold of 68 the next mux word goes
    to luma, with 64 to chroma first. The words after it go to different
    substreams; both readings still parse the slice."""
    def plan(b):
        for c in 'ZZZRPZZQ':
            b.group(**{'Z': dict(res=ZERO), 'R': R16, 'P': P16, 'Q': Q16}[c])
    return emit('oq33_mux16', 'mux16', pps16(24, (3, 3, 0)), plan, '64', vary=['low_min'],
                assumes={'prefix16_scope': 'qlevel', 'prefix16_cut': 'longer', 'prefix16': '13',
                         'chroma_qlevel': 'equal-depth'})


def oq34_flat_top():
    """33x2, 24 bpp, every range 4-12 (adjustedMaxQp 13), flatness flags
    sendable at every QP. Line 1: MPP groups keep bitSaveMode 2, the QP
    rises by 2 a step to 13. Group 19 sends flag 1 and group 20 type 0,
    position 0: group 21, the last, is somewhat flat. The group before it
    decoded at 13, above range 14's maximum 12. equal: adjusted to 13 - 4 =
    9; at-or-above: not adjusted, 13."""
    flats = {19: (1, 0, 0), 20: (1, 0, 0)}
    small_mpp = dict(res=[[0, -1, 0], [0, -1, 0], [0, -1, 0]], mpp=(True, True, True))

    def plan(b):
        for i, c in enumerate('S' * 12 + 'M' * 5 + 'N' * 4 + 'Y'):
            b.group(flat=flats.get(i), **{'S': SMALL, 'M': MPP3, 'N': small_mpp,
                                          'Y': dict(res=[[1, 1, 1], [0, 0, 0], [0, 0, 0]])}[c])
    pps = PPS(version=2, width=33, height=2, bpp16=384, ranges=((4, 12, 0),) * 15, flat_min=0, flat_max=15)
    return emit('oq34_flat_top', 'flat_top', pps, plan, 'at-or-above', vary=['activity_qp', 'flat_rerun'],
                assumes=dict(BITSAVE_FRAME, bitsave_flat='lagged'))


def oq35_prefix16_scope():
    """16 bpc, 24x1, every range pins QP 3 (luma qLevel 1, maximum size 15).
    Zero groups bring the predicted size to 0; the last group is luma MPP.
    qlevel: its prefix is cut at 15 bits, all zeros meaning MPP, and three
    15-bit residuals follow. qp0: the prefix is not cut there; after the
    fifteen zeros the next bit (the first residual's sign, 1) ends it, the
    size is 15 (MPP) and the residuals are read one bit later."""
    def plan(b):
        for _ in range(7):
            b.group(res=ZERO)
        b.group(**P16)
    return emit('oq35_prefix16_scope', 'prefix16_scope', pps16(24, (3, 3, 0)), plan, 'qlevel',
                vary=['prefix16', 'prefix16_cut'])


def oq36_prefix16_cut():
    """16 bpc, 27x1, every range 4-5, so the groups decode at QP 4 (luma
    qLevel 1, cut at 15 bits). With predicted sizes of 2 or more the uncut
    luma prefix is at most 14 bits. Group 7 is ICH and group 8, the last,
    continues it (prefix 1). longer: nothing is cut, ICH is signaled and
    continued as usual. always: the rules of the cut prefix apply in both
    groups: no ICH, and the prefix of group 8 is not adjusted."""
    wide = dict(res=[[100, -90, 80], [50, -40, 30], [50, -40, 30]])

    def plan(b):
        for c in 'SSSSSWS':
            b.group(**(wide if c == 'W' else SMALL))
        b.group(ich=(0, 1, 2))
        b.group(ich=(0, 0, 0))
    return emit('oq36_prefix16_cut', 'prefix16_cut', pps16(27, (4, 5, 0)), plan, 'longer',
                vary=['prefix16'], assumes={'prefix16_scope': 'qlevel', 'chroma_qlevel': 'equal-depth'})


def scale_input(name, question, construct, readings, version, width, height, scale, step,
                vary, common=None, assumes=None):
    """Pinned ranges (range 0: QP 0; ranges 1-14: QP 4), 24 bpp, the
    initial scale decremented every group, no flatness. The last group of
    the slice is the decisive one; the group before it has zero residuals,
    so the last group's predicted sizes are 0 whatever its QP. The two
    readings give different scales after group `step`, the same ones before;
    threshold 0 is solved to lie between their rcModelFullness values there,
    above every earlier value, so that with the range lag (OQ-11) only the
    last group's QP depends on the reading: 0 or 4."""
    groups = (width + 2) // 3 * height

    def plan(b):
        for _ in range(groups - 2):
            b.group(**SMALL)
        b.group(res=ZERO)
        b.group(**LAST)

    def pps_for(threshold):
        return PPS(version=version, width=width, height=height, bpp16=384, scale=scale, scale_dec=1,
                   thresholds=tuple(range(threshold, threshold + 14)),
                   ranges=((0, 0, 0),) + ((4, 4, 0),) * 14, flat_min=15, flat_max=15)
    logs = {}
    for reading in readings:
        probe = Builder(pps_for(113), dict(assumes or {}, **{question: reading}))
        plan(probe)
        logs[reading] = [r['model'] for r in probe.rc.log]
    lower, higher = readings
    low = max(logs[higher][:step] + logs[lower][:step + 1])
    high = logs[higher][step]
    threshold = (low + 8192) // 64 + 1
    assert threshold * 64 - 8192 < high, (name, 'no threshold between the readings', low, high)
    return emit(name, question, pps_for(threshold), plan, construct, vary=vary,
                assumes=dict(MODEL_TIMING, **(assumes or {})), common=common)


def oq42_scale_first():
    """OQ-42, DSC 1.2. 30x1 (ten groups), initial_scale_value 16
    (rcXformScale 2). With the first group decrementing (group) the scale
    after group 6 is 16 - 7 = 9, without it (not) 10: rcModelFullness, which
    is negative, is then higher under group, and group 9 decodes at QP 4
    (group) or 0 (not)."""
    return scale_input('oq42_scale_first', 'scale_first', 'not', ('not', 'group'), 2, 30, 1, 16, 6,
                       vary=['bpg_combine', 'target_floor', 'scale_line'])


def oq42b_scale_first():
    """OQ-42, DSC 1.1: oq42_scale_first as a DSC 1.1 stream."""
    return scale_input('oq42b_scale_first', 'scale_first', 'not', ('not', 'group'), 1, 30, 1, 16, 6,
                       vary=['scale_line'])


def oq43_scale_line():
    """OQ-43, DSC 1.2. 12x2 (four groups a line), initial_scale_value 16:
    after the first line the scale is 13 (groups 1-3 decremented it, under
    OQ-42's reading not, which the input assumes). first
    keeps 13; until-unity goes on, to 12 after group 4. Group 7, the last,
    decodes at QP 0 (first) or 4 (until-unity)."""
    return scale_input('oq43_scale_line', 'scale_line', 'first', ('first', 'until-unity'), 2, 12, 2, 16, 4,
                       vary=['bpg_combine', 'target_floor'], assumes={'scale_first': 'not', 'line_flat': 'signaled'})


def oq43b_scale_line():
    """OQ-43, DSC 1.1: oq43_scale_line as a DSC 1.1 stream."""
    return scale_input('oq43b_scale_line', 'scale_line', 'first', ('first', 'until-unity'), 1, 12, 2, 16, 4,
                       vary=[], assumes={'scale_first': 'not'})


def build_all():
    return [make() for make in (oq7_bpg_combine, oq20_chroma_qlevel, oq21_prefix16,
                                oq22_bitsave_ich, oq23_bitsave_pred_next, oq24_bitsave_flat,
                                oq25_line_flat, oq24b_bitsave_flat, oq24c_bitsave_flat,
                                oq24d_bitsave_flat, oq24e_bitsave_flat, oq26_low_min,
                                oq27_decrement_test, oq28_activity_qp, oq29_bitsave_step,
                                oq30_target_floor, oq31_flat_rerun, oq32_rerun_bitsave, oq33_mux16,
                                oq34_flat_top, oq35_prefix16_scope, oq36_prefix16_cut,
                                oq42_scale_first, oq42b_scale_first, oq43_scale_line, oq43b_scale_line)]


if __name__ == '__main__':
    import json
    for name, entry in build_all():
        print(name, json.dumps({k: v['qp_schedule'] for k, v in entry['readings'].items()}))
