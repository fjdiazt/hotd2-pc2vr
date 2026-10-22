import importlib.util
from pathlib import Path
import struct
import unittest

spec = importlib.util.spec_from_file_location('capture_bats', Path(__file__).parents[1] / 'tools/capture_bats.py')
capture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(capture)


class ActorSnapshotTests(unittest.TestCase):
    def test_native_node_layout_and_reject_partial_snapshot(self):
        raw = bytearray(0x13D8)
        struct.pack_into('<h', raw, 0x1F4, 0x1F)
        struct.pack_into('<f', raw, 0x1300, .7)
        struct.pack_into('<I', raw, 0x20C + 0x90, 0x1B03)
        struct.pack_into('<16f', raw, 0x20C + 0x90 + 0x28, *range(16))
        data = struct.pack('<8I', 0x54434142, 1, 100, 1, 0x123456, 0x42F660, 9, len(raw)) + raw
        report = capture.actor_report(data)
        self.assertEqual(report['actors'][0]['nodes'][0]['model'], '0x1b03')
        self.assertEqual(report['actors'][0]['nodes'][0]['model_view'][12:15], (12., 13., 14.))
        for bad in (b'', data[:20], data[:-1], data + b'bad', struct.pack('<4I', 0, 1, 100, 0)):
            with self.assertRaises(ValueError):
                capture.actor_report(bad)


if __name__ == '__main__':
    unittest.main()
