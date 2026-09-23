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
EXE = Path(sys.argv[1] if len(sys.argv) > 1 else './dscdecode').resolve()
READINGS = {
    'flat_restart': ('next-cycle', 'in-flight'),
    'threshold_eq': ('lower', 'upper'),
    'frac_reset': ('chunk', 'literal'),
    'delay_offset': ('inclusive', 'exclusive'),
}
# The statistic that shows each question's condition occurred.
EXERCISED = {
    'flat_restart': 'flat_queue_differs',
    'threshold_eq': 'threshold_equal',
    'frac_reset': 'frac_differs',
}


def decode(name, readings, out):
    args = [str(EXE), '--stats']
    for key, value in readings.items():
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
            for values in product(*READINGS.values()):
                readings = dict(zip(READINGS, values))
                stats = decode(name, readings, out)
                assert stats[EXERCISED[question]] > 0, (name, stats)
                own = readings[question]
                expected = (ROOT / f'{name}.{own}.expected.ppm').read_bytes()
                assert out.read_bytes() == expected, (name, readings)
                outputs[own] = expected
                count += 1
            assert len(set(outputs.values())) == 2, name
            print(f'PASS {name}: {question} readings give their predicted, '
                  f'different outputs under all 16 combinations')
    print(f'{count} discriminator decodes passed')


if __name__ == '__main__':
    main()
