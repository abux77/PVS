#!/usr/bin/env python3
"""SAM regression fixtures captured from the former Python reference; no reference dependency."""
import ctypes as C
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = json.loads((ROOT / 'tests/sam_behavior_golden.json').read_text())

class Phonemes(C.Structure):
    _fields_ = [('index', C.c_uint8 * 1024), ('stress', C.c_uint8 * 1024),
                ('length', C.c_uint8 * 1024), ('count', C.c_size_t)]

SINK = C.CFUNCTYPE(C.c_bool, C.POINTER(C.c_uint8), C.c_size_t, C.c_void_p)

class ReferenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='pvs-sam-test-')
        lib = Path(cls.temp.name) / 'sam.so'
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-O2', '-shared', '-fPIC',
                        '-I' + str(ROOT / 'src/sam'), '-I' + str(ROOT / 'third_party/sam'),
                        str(ROOT / 'third_party/sam/render.c'),
                        *map(str, [ROOT / 'src/sam' / ('sam_' + name + '.c') for name in ['reciter','parser','renderer']]),
                        '-o', str(lib)], check=True)
        cls.lib = C.CDLL(str(lib))
        cls.lib.sam_recite.argtypes = [C.c_char_p, C.c_void_p, C.c_size_t]
        cls.lib.sam_recite.restype = C.c_bool
        cls.lib.sam_parse.argtypes = [C.c_char_p, C.POINTER(Phonemes)]
        cls.lib.sam_parse.restype = C.c_bool
        cls.lib.sam_parse_recited.argtypes = cls.lib.sam_parse.argtypes
        cls.lib.sam_parse_recited.restype = C.c_bool
        cls.lib.sam_render.argtypes = [C.POINTER(Phonemes), C.c_uint8, C.c_uint8, C.c_uint8, C.c_uint8, SINK, C.c_void_p]
        cls.lib.sam_render.restype = C.c_bool
        cls.lib.sam_quantize.argtypes = [C.c_uint8, C.c_uint8]
        cls.lib.sam_quantize.restype = C.c_uint8

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def recite(self, text):
        output = C.create_string_buffer(4096)
        self.assertTrue(self.lib.sam_recite(text.encode(), output, len(output)))
        return output.value.decode()

    def parse(self, text, strict=True):
        phonemes = Phonemes()
        parser = self.lib.sam_parse if strict else self.lib.sam_parse_recited
        self.assertTrue(parser(text.encode(), C.byref(phonemes)), text)
        return phonemes

    def render(self, p, voice):
        output = bytearray()
        @SINK
        def sink(data, n, unused):
            self.assertLessEqual(n, 128)
            output.extend(C.string_at(data, n))
            return True
        self.assertTrue(self.lib.sam_render(C.byref(p), *voice, sink, None))
        return output

    def test_reciter_rules_and_exceptions(self):
        for text, expected in FIXTURES['reciter'].items():
            with self.subTest(text=text):
                self.assertEqual(self.recite(text), expected)

    def test_phoneme_pipeline(self):
        for text, (idx, length, stress) in FIXTURES['phonemes'].items():
            with self.subTest(text=text):
                p = self.parse(text, strict=False)
                self.assertEqual(list(p.index[:p.count+1]), idx)
                self.assertEqual(list(p.length[:p.count+1]), length)
                self.assertEqual(list(p.stress[:p.count+1]), stress)

    def test_formant_multiply_matches_legacy(self):
        trans = self.lib.trans
        trans.argtypes = [C.c_uint8, C.c_uint8]
        trans.restype = C.c_uint8
        # Emulate the old eight rotate/add steps, including carry and byte wrap.
        for a in range(256):
            for b in range(256):
                multiplier, high = a, 0
                for _ in range(8):
                    carry = multiplier & 1
                    multiplier >>= 1
                    if carry:
                        total = high + b
                        high = total & 255
                        carry = int(total > 255)
                    high = (high >> 1) | (carry << 7)
                expected = (high << 1) & 255
                self.assertEqual(trans(a, b), expected, (a, b))

    def test_c_renderer_deterministic(self):
        # Sound comes from the existing upstream C renderer, per user request.
        # Interleave other utterances to catch stale global frame/output state.
        corpus = ['/HEH4LOW', 'WERLD', 'AY4', 'AEM', 'AH', 'ROH4BOWLAORD', 'SIH4KS',
                  'ZIY4ROW', 'SH', 'F', 'TH', '/H', '/X', 'Z', 'ZH', 'V', 'DH',
                  'CH', 'J', 'UL', 'UM', 'UN', 'AE8R,', 'AO2L?', 'IY3.', 'PREH4SEHT']
        voices = [(72,64,128,128), (50,10,1,255), (255,255,255,1),
                  (1,1,128,128), (110,90,160,90)]
        for text in corpus:
            p = self.parse(text)
            for voice in voices:
                with self.subTest(text=text, voice=voice):
                    first = self.render(p, voice)
                    self.assertGreater(len(first), 0)
                    self.render(self.parse('AH4'), (99,80,90,140))
                    self.assertEqual(self.render(p, voice), first)

    def test_original_c_renderer_samples(self):
        # Captured from the user's unmodified upstream render.c at commit
        # a7b36ef, with zero-initialized PCM and the same phoneme/voice inputs.
        # The fixtures verify exact streaming equivalence, not Python PCM.
        fixtures = json.loads((ROOT / 'tests/sam_c_renderer_golden.json').read_text())
        for fixture in fixtures:
            with self.subTest(phonemes=fixture['phonemes'], voice=fixture['voice']):
                pcm = self.render(self.parse(fixture['phonemes']), fixture['voice'])
                self.assertEqual(len(pcm), fixture['samples'])
                self.assertEqual(hashlib.sha256(pcm).hexdigest(), fixture['sha256'])

    def test_bit_crusher_fixtures(self):
        for bits, expected in FIXTURES['quantize'].items():
            self.assertEqual(list(self.lib.sam_quantize(s,int(bits)) for s in range(256)), expected)

    def test_bounds_and_cancellation(self):
        p = Phonemes()
        for text in ['1AH', 'AH$', 'AH*', 'AH'*2000]:
            self.assertFalse(self.lib.sam_parse(text.encode(), C.byref(p)))
        output = C.create_string_buffer(8)
        self.assertFalse(self.lib.sam_recite(b'hello world', output, len(output)))
        self.assertFalse(self.lib.sam_recite(b'A'*385, output, len(output)))
        p = self.parse('AH4')
        @SINK
        def cancel(data, n, unused): return False
        self.assertFalse(self.lib.sam_render(C.byref(p),72,64,128,128,cancel,None))
        p = self.parse('AH4'*200)
        self.assertGreater(len(self.render(p,(72,64,128,128))), 100000)

if __name__ == '__main__':
    unittest.main()
