#!/usr/bin/env python3
"""Decode each discriminator under every combination of readings.

The expected output for each reading of a discriminator's own question comes
from tests/make_discriminators.py, a separate model of the rate control. The
decoder must reproduce it under every setting of the other switches, and its
statistics must show that the question was actually exercised.
"""
from itertools import product
from pathlib import Path
import json
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent / 'discriminators'
EXE = Path(sys.argv[1] if len(sys.argv) > 1 else 'build/release/dscdecode').resolve()
READINGS = {
    'flat_restart': ('next-cycle', 'in-flight'),
    'threshold_eq': ('lower', 'upper'),
    'frac_reset': ('chunk', 'literal'),
    'delay_offset': ('inclusive', 'exclusive'),
    'bp_left': ('replicate', 'midpoint'),
    'bp_edge': ('window', 'before'),
    'bp_sad': ('shift', 'clip'),
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
}
# The statistic that shows each question's condition occurred.
EXERCISED = {
    'flat_restart': 'flat_queue_differs',
    'threshold_eq': 'threshold_equal',
    'frac_reset': 'frac_differs',
    'bp_left': 'bp_left_differs',
    'delay_partial': 'delay_partial_differs',
}


def decode(name, readings, out, assumes):
    args = [str(EXE), '--stats']
    for key, value in {**assumes, **readings}.items():
        args += ['--reading', f'{key}={value}']
    args += [ROOT / f'{name}.pps', ROOT / f'{name}.bin', out]
    p = subprocess.run(list(map(str, args)), capture_output=True, text=True, timeout=10)
    assert p.returncode == 0, (name, readings, p.stderr)
    line = next(l for l in p.stderr.splitlines() if l.startswith('stats:'))
    return {k: int(v) for k, v in (f.split('=') for f in line.split()[1:])}


def main():
    manifest = json.loads((ROOT / 'manifest.json').read_text())
    count = 0
    with tempfile.TemporaryDirectory(prefix='dsc-disc-') as tmp:
        out = Path(tmp) / 'out.ppm'
        for name, entry in manifest.items():
            question = entry['question']
            outputs = {}
            vary = entry['vary']
            # The question's readings are those the input has predictions
            # for; a reading added after the model decoded it has none.
            choices = {k: list(entry['readings']) if k == question else READINGS[k] for k in vary}
            for values in product(*(choices[k] for k in vary)):
                readings = dict(zip(vary, values))
                stats = decode(name, readings, out, entry.get('assumes', {}))
                if question in EXERCISED:
                    assert stats[EXERCISED[question]] > 0, (name, stats)
                own = readings[question]
                expected = (ROOT / f'{name}.{own}.expected.ppm').read_bytes()
                assert out.read_bytes() == expected, (name, readings)
                outputs[own] = expected
                count += 1
            # A question with three readings may share one prediction
            # between two of them in a given input.
            assert len(set(outputs.values())) >= 2, name
            combos = len(list(product(*(choices[k] for k in vary))))
            # A superseded input stays a decoder test under its own assumptions.
            note = (f' (superseded by {entry["superseded_by"]}; decoded under the '
                    f'readings it assumes)' if 'superseded_by' in entry else '')
            print(f'PASS {name}: {question} readings give their predicted, '
                  f'different outputs under all {combos} combinations{note}')
    print(f'{count} discriminator decodes passed')


if __name__ == '__main__':
    main()
