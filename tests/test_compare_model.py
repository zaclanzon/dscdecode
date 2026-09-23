#!/usr/bin/env python3
"""tools/compare_model plumbing, against tests/fake_model.py.

These check the harness: SKIP handling, container wrapping and splitting,
config and list files, first-difference reporting and exit codes. They say
nothing about the reference model, which CI never has.
"""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent
TOOL = ROOT / 'tools' / 'compare_model'
FAKE = ROOT / 'tests' / 'fake_model.py'


def run(args, **env):
    e = {k: v for k, v in os.environ.items() if k != 'DSCDECODE_MODEL_BIN'}
    e.update(env)
    p = subprocess.run([sys.executable, str(TOOL), *map(str, args)], capture_output=True,
                       text=True, env=e, timeout=120)
    return p.returncode, p.stdout + p.stderr


def png(path, w, h, pixels, depth=8):
    """Minimal PNG writer using every filter type in turn (test data only)."""
    import struct, zlib
    step = depth // 8
    bpp = 3 * step
    rows, prev = [], bytes(w * bpp)
    for y in range(h):
        line = b''.join(v.to_bytes(step, 'big') for v in pixels[y * w * 3:(y + 1) * w * 3])
        f = y % 5
        out = bytearray([f])
        for i in range(len(line)):
            a = line[i - bpp] if i >= bpp else 0
            b, c = prev[i], prev[i - bpp] if i >= bpp else 0
            pred = [0, a, b, (a + b) // 2][f] if f < 4 else (
                lambda p: a if abs(p - a) <= abs(p - b) and abs(p - a) <= abs(p - c)
                else b if abs(p - b) <= abs(p - c) else c)(a + b - c)
            out.append((line[i] - pred) & 255)
        rows.append(bytes(out))
        prev = line
    chunk = lambda k, d: struct.pack('>I', len(d)) + k + d + struct.pack('>I', zlib.crc32(k + d))
    Path(path).write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, depth, 2, 0, 0, 0))
                           + chunk(b'IDAT', zlib.compress(b''.join(rows))) + chunk(b'IEND', b''))


def corpus_checks(tmp, fake):
    import importlib.machinery
    rc = importlib.machinery.SourceFileLoader('run_corpus', str(ROOT / 'tools/run_corpus')).load_module()
    corpus = Path(tmp) / 'corpus'
    corpus.mkdir()
    w, h = 7, 6
    pixels = [(x * 37 + y * 11 + k * 90) % 256 for y in range(h) for x in range(w) for k in range(3)]
    png(corpus / 'a.png', w, h, pixels)
    png(corpus / 'b16.png', w, h, [v * 257 for v in pixels], depth=16)
    rc.png_to_ppm(corpus / 'a.png', Path(tmp) / 'a.ppm')
    rc.png_to_ppm(corpus / 'b16.png', Path(tmp) / 'b.ppm')
    expected = f'P6\n{w} {h}\n255\n'.encode() + bytes(pixels)
    assert (Path(tmp) / 'a.ppm').read_bytes() == expected
    assert (Path(tmp) / 'b.ppm').read_bytes() == expected
    (corpus / 'c.ppm').write_bytes(expected)
    (corpus / 'd.pgm').write_bytes(f'P5\n{w} {h}\n255\n'.encode() + bytes(pixels[::3]))
    print('PASS PNG reader: all five filter types, 8 and 16 bit')
    cfg = Path(tmp) / 'cfg'
    env = dict(fake, DSCDECODE_MODEL_CFG_DIR=str(cfg))
    e = {k: v for k, v in os.environ.items() if k != 'DSCDECODE_MODEL_BIN'}
    e.update(env)
    p = subprocess.run([sys.executable, str(ROOT / 'tools/run_corpus'), '--dir', str(corpus),
                        '--min-images', '4', '--bp', '0', '--slices', '1', '2'],
                       capture_output=True, text=True, env=e, timeout=300)
    assert p.returncode == 0, p.stdout + p.stderr
    assert p.stdout.count('| match |') == 8 and '4 images, 8 runs' in p.stdout, p.stdout
    p = subprocess.run([sys.executable, str(ROOT / 'tools/run_corpus'), '--dir', str(ROOT / 'tests')],
                       capture_output=True, text=True, env=e, timeout=60)
    assert p.returncode == 2 and 'inside the repository' in p.stderr, p.stdout + p.stderr
    print('PASS run_corpus: 4 images x 2 settings; refuses a corpus inside the repo')


def main():
    exe = str(Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / 'dscdecode').resolve())
    with tempfile.TemporaryDirectory(prefix='dsc-cm-') as tmp:
        common = dict(DSCDECODE_RUNS=tmp, DSCDECODE_BIN=exe, DSCDECODE_MODEL_CFG_DIR=str(ROOT / 'tests'))
        code, out = run(['--self-test'], **common)
        assert code == 77 and 'SKIP' in out, out
        code, out = run(['bitstream', ROOT / 'tests/fixtures/gradient.pps',
                         ROOT / 'tests/fixtures/gradient.bin'], **common)
        assert code == 77 and 'SKIP' in out, out
        print('PASS SKIP (exit 77) without DSCDECODE_MODEL_BIN')
        fake = dict(common, DSCDECODE_MODEL_BIN=str(FAKE))
        code, out = run(['bitstream', ROOT / 'tests/fixtures/color_crop.pps',
                         ROOT / 'tests/fixtures/color_crop.bin'], **fake)
        assert code == 0 and 'match (bit-exact)' in out, out
        print('PASS bitstream mode: DSCF wrapping, model decode, match')
        # color_crop is 187x5; the stub alters sample (5, 2, G) by +3.
        code, out = run(['bitstream', ROOT / 'tests/fixtures/color_crop.pps',
                         ROOT / 'tests/fixtures/color_crop.bin'],
                        FAKE_MODEL_CORRUPT='5,2,1,3', **fake)
        assert code == 1 and 'MISMATCH first at x=5 y=2 G' in out, out
        assert '1 samples in 1 pixels differ' in out, out
        print('PASS mismatch: first differing pixel and diff count reported, exit 1')
        code, out = run(['bitstream', '--all-readings', '--vary',
                         'flat_restart,threshold_eq,frac_reset,delay_offset',
                         ROOT / 'tests/fixtures/qp_flatness.pps',
                         ROOT / 'tests/fixtures/qp_flatness.bin'], **fake)
        assert code == 0 and out.count('match (bit-exact)') == 16, out
        code, out = run(['bitstream', '--all-readings', '--vary',
                         'bp_left,bp_edge,bp_sad,incr_order,rc_pipeline,scale_dec,partial_target',
                         ROOT / 'tests/fixtures/bp_left_edge.pps',
                         ROOT / 'tests/fixtures/bp_left_edge.bin'], **fake)
        assert code == 0 and out.count('match (bit-exact)') == 128, out
        code, out = run(['bitstream', '--all-readings', ROOT / 'tests/fixtures/flat.pps',
                         ROOT / 'tests/fixtures/flat.bin'], **fake)
        assert code == 2 and 'choose switches with --vary' in out, out
        print('PASS --all-readings: 16 and 128 decodes; unbounded product refused')
        # Image mode needs a config template; this minimal one is the test's own.
        cfg = Path(tmp) / 'cfg'
        cfg.mkdir()
        (cfg / 'test_dsc_1_1.cfg').write_text('FUNCTION 0\nSRC_LIST x.txt\n//SLICE_WIDTH 1\n'
                                              'SLICE_HEIGHT 108\nINCLUDE rc_8bpc_12bpp.cfg\n')
        (cfg / 'rc_8bpc_8bpp.cfg').write_text('BITS_PER_PIXEL 8\n')
        image = Path(tmp) / 'image.ppm'
        image.write_bytes(b'P6\n4 2\n255\n' + bytes(24))
        code, out = run(['image', image, '--slices', '2', '--run-dir', Path(tmp) / 'im'],
                        DSCDECODE_MODEL_CFG_DIR=str(cfg), **{k: v for k, v in fake.items()
                                                             if k != 'DSCDECODE_MODEL_CFG_DIR'})
        assert code == 0 and 'match (bit-exact)' in out, out
        written = (Path(tmp) / 'im' / 'encode.cfg').read_text().split('\n')
        assert 'SLICE_WIDTH 2' in written and 'FUNCTION 1' in written, written
        assert written.index('INCLUDE rc_8bpc_8bpp.cfg') < written.index('BITS_PER_PIXEL 8'), written
        print('PASS image mode: config, encode, split, both decodes')
        code, out = run(['--self-test'], DSCDECODE_MODEL_CFG_DIR=str(cfg),
                        **{k: v for k, v in fake.items() if k != 'DSCDECODE_MODEL_CFG_DIR'})
        assert code == 0 and 'match (bit-exact)' in out, out
        print('PASS --self-test with a model configured')
        # The stub decodes with one chosen reading per question, which must be
        # the one reported. With the M1 pipeline readings, every input built
        # under them gets a verdict, and oq2b, built under the decoder's
        # current defaults, matches neither: a failure. oq2 is superseded by
        # oq2b: its match is printed for the record and never counted.
        m1 = ('incr_order=printed rc_pipeline=same-group scale_dec=from-group-1 '
              'partial_target=three very_flat=group-qp partial_padding=reject flat_max_qp=own')
        chosen = 'flat_restart=next-cycle threshold_eq=lower frac_reset=chunk bp_left=replicate '
        superseded = ('oq2_threshold_equality (threshold_eq): SUPERSEDED by '
                      'oq2b_threshold_equality; not part of the verdict')
        code, out = run(['discriminators'], FAKE_MODEL_READINGS=chosen + m1, **fake)
        for line in ('oq1_flat_restart (flat_restart): model output matches next-cycle',
                     superseded, 'for the record, model output matches lower',
                     'oq2b_threshold_equality (threshold_eq): model output matches NEITHER',
                     'oq3_fractional_bpp (frac_reset): model output matches chunk',
                     'oq4_bp_left (bp_left): model output matches replicate'):
            assert line in out, out
        assert 'oq2_threshold_equality (threshold_eq): model output matches' not in out, out
        assert 'inconclusive' not in out and code == 1, out
        # With the default pipeline readings, oq2b gets the verdict. oq2
        # matches neither prediction, which is not a failure: it is superseded.
        code, out = run(['discriminators'], FAKE_MODEL_READINGS=chosen, **fake)
        for line in ('oq1_flat_restart (flat_restart): model output matches next-cycle',
                     'oq2b_threshold_equality (threshold_eq): model output matches lower',
                     'oq3_fractional_bpp (frac_reset): model output matches chunk',
                     'oq4_bp_left (bp_left): model output matches replicate',
                     superseded, 'for the record, model output matches NEITHER prediction'):
            assert line in out, out
        assert 'inconclusive' not in out and code == 0, out
        # A stub output that matches no prediction: inputs built under the M1
        # readings (oq1, oq3) are inconclusive, the others fail the run.
        code, out = run(['discriminators'], FAKE_MODEL_READINGS=chosen,
                        FAKE_MODEL_CORRUPT='0,0,0,1', **fake)
        for name in ('oq1_flat_restart (flat_restart)', 'oq3_fractional_bpp (frac_reset)',
                     'oq2b_threshold_equality (threshold_eq)', 'oq4_bp_left (bp_left)'):
            assert f'{name}: model output matches NEITHER' in out, out
        assert out.count('inconclusive: built assuming incr_order=printed') == 2, out
        assert superseded in out and code == 1, out
        print('PASS discriminators mode: verdict per prediction; stale assumptions '
              'inconclusive; superseded input left out')
        corpus_checks(tmp, fake)
    print('compare_model harness checks passed (fake model; not a model comparison)')


if __name__ == '__main__':
    main()
