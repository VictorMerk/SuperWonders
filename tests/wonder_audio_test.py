"""Silent, empty and truncated WAVs must not count as observed game audio."""
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import wave

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools' / 'perf'))
from check_wonder_audio import check_audio


class AudioTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.path = Path(self.tmp.name) / 'audio.wav'

    def write(self, samples, width=2):
        with wave.open(str(self.path), 'wb') as stream:
            stream.setnchannels(2)
            stream.setsampwidth(width)
            stream.setframerate(48000)
            stream.writeframes(struct.pack('<' + 'h' * len(samples), *samples))

    def test_empty_and_silent_captures_fail(self):
        for samples in ([], [0] * 32):
            self.write(samples)
            self.assertFalse(check_audio(self.path)['non_silent_audio_observed'])

    def test_activity_after_silence_is_found_and_measured(self):
        self.write([0] * 40000 + [-32768, 32767])
        result = check_audio(self.path)
        self.assertTrue(result['non_silent_audio_observed'])
        self.assertEqual(result['peak'], 32768)
        self.assertEqual(result['nonzero_samples'], 2)
        self.assertEqual(result['frames'], 20001)
        self.assertGreater(result['rms'], 0)

    def test_truncated_data_fails_even_if_samples_are_nonzero(self):
        self.write([100] * 32)
        self.path.write_bytes(self.path.read_bytes()[:-4])
        with self.assertRaisesRegex(ValueError, 'Truncated'):
            check_audio(self.path)

    def test_unsupported_sample_format_fails(self):
        self.write([100] * 32, width=1)
        with self.assertRaisesRegex(ValueError, '16-bit'):
            check_audio(self.path)


if __name__ == '__main__':
    unittest.main()
