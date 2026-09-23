#!/usr/bin/env python3
"""Compare decoder output to independently specified fixture pixels."""
from pathlib import Path
import itertools
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent
FIXTURES = ROOT/'fixtures'
EXE = Path(sys.argv[1] if len(sys.argv) > 1 else './dscdecode').resolve()


def invoke(arguments, expect_success=True):
    p = subprocess.run([str(EXE),*map(str,arguments)], capture_output=True, timeout=10)
    if expect_success:
        assert p.returncode == 0, p.stderr.decode(errors='replace')
    else:
        assert p.returncode > 0, (p.returncode,p.stderr)


def main():
    count = 0
    with tempfile.TemporaryDirectory(prefix='dsc-tests-') as temporary:
        tmp = Path(temporary)
        for name in ('flat','gradient','checker','mpp','ich','color_crop',
                     'qp_transition','qp_flatness'):
            out = tmp/f'{name}.ppm'
            invoke([FIXTURES/f'{name}.pps',FIXTURES/f'{name}.bin',out])
            actual, expected = out.read_bytes(), (FIXTURES/f'{name}.expected.ppm').read_bytes()
            assert actual == expected, f'{name}: pixels/header mismatch'
            count += 1
            print(f'PASS {name}: exact expected PPM')
        # Block prediction (research/bp-worked-note.md): the expected image
        # must come out under every BP reading, since the fixtures avoid the
        # open BP questions.
        for name in ('bp_left_edge', 'bp_slice_boundary'):
            expected = (FIXTURES/f'{name}.expected.ppm').read_bytes()
            for left, edge, sad in itertools.product(('replicate','midpoint'),
                                                     ('window','before'), ('shift','clip')):
                out = tmp/f'{name}.ppm'
                invoke(['--reading',f'bp_left={left}','--reading',f'bp_edge={edge}',
                        '--reading',f'bp_sad={sad}',FIXTURES/f'{name}.pps',FIXTURES/f'{name}.bin',out])
                assert out.read_bytes() == expected, f'{name}: {left} {edge} {sad}'
            count += 1
            print(f'PASS {name}: exact expected PPM under all 8 BP readings')
        out = tmp/'slice.ppm'
        invoke(['--slice',FIXTURES/'checker.pps',FIXTURES/'checker.slice0.bin',out])
        expected = b'P6\n96 3\n255\n'+bytes(
            c for y in range(3) for x in range(96)
            for c in [120+16*((x//5+y)%2)]*3)
        assert out.read_bytes() == expected, 'single-slice mismatch'
        count += 1
        print('PASS independent first checker slice')
        for length in (0,1,17,18,100,287):
            bad = tmp/'truncated.bin'
            bad.write_bytes((FIXTURES/'flat.bin').read_bytes()[:length])
            invoke([FIXTURES/'flat.pps',bad,tmp/'bad.ppm'],False)
            count += 1
        print('PASS six truncated frame payloads rejected')
        bad = tmp/'short.pps'
        bad.write_bytes((FIXTURES/'flat.pps').read_bytes()[:127])
        invoke([bad,FIXTURES/'flat.bin',tmp/'bad.ppm'],False)
        count += 1
        print('PASS short PPS rejected')
        # §6.6 padding: the reject reading (OQ-17) enforces it; these fixtures
        # were written for that reading.
        for name in ('invalid_partial_residual', 'invalid_partial_ich'):
            out = tmp/f'{name}.ppm'
            out.write_bytes(b'previous output must survive a failed decode')
            invoke(['--reading','partial_padding=reject',FIXTURES/f'{name}.pps',FIXTURES/f'{name}.bin',out], False)
            assert out.read_bytes() == b'previous output must survive a failed decode'
            count += 1
            print(f'PASS {name}: rejected without overwriting output (partial_padding=reject)')
        # The default accept reading decodes them; the padding produces no
        # pixel, so both are uniform gray 128 at 95x3.
        for name in ('invalid_partial_residual', 'invalid_partial_ich'):
            out = tmp/f'{name}.ppm'
            invoke([FIXTURES/f'{name}.pps',FIXTURES/f'{name}.bin',out])
            assert out.read_bytes() == b'P6\n95 3\n255\n' + bytes([128]) * (95*3*3), name
            count += 1
            print(f'PASS {name}: padding ignored under the default partial_padding=accept')
    print(f'{count} checks passed')


if __name__ == '__main__':
    main()
