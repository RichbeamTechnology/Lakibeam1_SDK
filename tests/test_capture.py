"""Black-box regression tests. Generated fixtures contain no customer capture data."""
import csv
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

EXE = Path(sys.argv.pop(1)).resolve() if len(sys.argv) > 1 else None


def payloads(scans=1, step=400, continuous=False):
    blocks = []
    packets = []
    for scan in range(scans):
        for a in range(0, 36000, step):
            records = b"".join(struct.pack("<HBHB", 1000 + scan, j + 1, 2000 + scan, 31 - j)
                               for j in range(16))
            blocks.append(struct.pack("<HH", 0xeeff, a) + records)
        if not continuous:
            while len(blocks) % 12:
                blocks.append(b"\xff" * 100)
    while len(blocks) % 12:
        blocks.append(b"\xff" * 100)
    for i in range(0, len(blocks), 12):
        packets.append(b"".join(blocks[i:i + 12]) + struct.pack("<IH", (0xffff0000 + i * 1000) & 0xffffffff, 0x4037))
    return packets


def frame(payload, source=(192, 168, 8, 2), port=2368, vlan=False, ihl=5, fragment=0, link=1):
    udp = struct.pack("!HHHH", 50599, port, len(payload) + 8, 0) + payload
    ip = struct.pack("!BBHHHBBH4s4s", 0x40 | ihl, 0, ihl * 4 + len(udp), 1,
                     fragment, 64, 17, 0, bytes(source), bytes((192, 168, 8, 1)))
    ip += b"\0" * (ihl * 4 - 20) + udp
    if link in (101, 228):
        return ip
    if link == 113:
        return b"\0" * 14 + b"\x08\x00" + ip
    if link == 276:
        return b"\x08\x00" + b"\0" * 18 + ip
    if link == 0:
        return struct.pack("<I", 2) + ip
    return b"\0" * 12 + (b"\x81\x00\x00\x01\x08\x00" if vlan else b"\x08\x00") + ip


def pcap(frames, endian="<", nano=False, link=1):
    magic = 0xa1b23c4d if nano else 0xa1b2c3d4
    output = struct.pack(endian + "IHHIIII", magic, 2, 4, 0, 0, 65535, link)
    for i, data in enumerate(frames):
        output += struct.pack(endian + "IIII", 1700000000, i * 1000, len(data), len(data)) + data
    return output


def ng_block(kind, data, endian="<"):
    data += b"\0" * (-len(data) % 4)
    n = len(data) + 12
    return struct.pack(endian + "II", kind, n) + data + struct.pack(endian + "I", n)


def pcapng(frames, endian="<", link=1, resolution=6, offset=0, kind=6, interface=0):
    output = ng_block(0x0a0d0d0a, struct.pack(endian + "IHHq", 0x1a2b3c4d, 1, 0, -1), endian)
    options = struct.pack(endian + "HH", 9, 1) + bytes([resolution]) + b"\0" * 3
    options += struct.pack(endian + "HHqHH", 14, 8, offset, 0, 0)
    for _ in range(interface + 1):
        output += ng_block(1, struct.pack(endian + "HHI", link, 0, 65535) + options, endian)
    for i, data in enumerate(frames):
        ticks = 1700000000123456 + i
        if kind == 3:
            body = struct.pack(endian + "I", len(data)) + data
        else:
            ident = struct.pack(endian + "I", interface) if kind == 6 else struct.pack(endian + "HH", interface, 0)
            body = ident + struct.pack(endian + "IIII", ticks >> 32, ticks & 0xffffffff, len(data), len(data)) + data
        output += ng_block(kind, body, endian)
    return output


class CaptureTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def run_capture(self, data, *options, expected=0):
        source = self.root / "capture test 中文.pcapng"
        source.write_bytes(data)
        destination = self.root / ("result_" + str(len(list(self.root.glob("result_*")))))
        result = subprocess.run([str(EXE), str(source), str(destination), "--quiet", *options],
                                capture_output=True, timeout=30)
        self.assertEqual(result.returncode, expected, result.stderr.decode(errors="replace"))
        summary = json.loads((destination / "summary.json").read_text())
        rows = []
        for path in sorted(destination.glob("points_*.csv")):
            with path.open(newline="") as f:
                rows.extend(csv.DictReader(f))
        return summary, rows, destination

    def verify_scan(self, rows, count=1440, distance=1000):
        self.assertEqual(len(rows), count)
        self.assertEqual([float(r["angle_deg"]) for r in rows], [i * 360 / count for i in range(count)])
        self.assertTrue(all(int(r["distance0_raw"]) == distance for r in rows))
        self.assertTrue(all(int(r["distance1_raw"]) == distance + 1000 for r in rows))
        self.assertEqual([int(r["rssi0"]) for r in rows[:16]], list(range(1, 17)))

    def test_one_circle_including_first_and_last(self):
        s, rows, _ = self.run_capture(pcapng([frame(p) for p in payloads()]))
        self.verify_scan(rows)
        self.assertEqual((s["complete_scans"], s["padding_blocks"], s["incomplete_scans"]), (1, 6, 0))
        self.assertEqual(rows[0]["capture_timestamp_s"], "1700000000.123456000")
        self.assertEqual(rows[-1]["angle_deg"], "359.75")

    def test_classic_endian_and_resolution(self):
        for endian in ("<", ">"):
            for nano in (False, True):
                with self.subTest(endian=endian, nano=nano):
                    _, rows, _ = self.run_capture(pcap([frame(p) for p in payloads()], endian, nano))
                    self.verify_scan(rows)
                    self.assertEqual(rows[192]["capture_timestamp_s"], "1700000000.000001000" if nano else "1700000000.001000000")

    def test_3600_mode_and_wrap_inside_packet(self):
        for continuous in (False, True):
            s, rows, _ = self.run_capture(pcapng([frame(p) for p in payloads(2, 160, continuous)]))
            self.assertEqual(s["complete_scans"], 2)
            self.verify_scan(rows[:3600], 3600, 1000)
            self.verify_scan(rows[3600:], 3600, 1001)

    def test_ng_endian_interfaces_and_packet_types(self):
        frames = [frame(p) for p in payloads()]
        for endian in ("<", ">"):
            for kind in (2, 3, 6):
                _, rows, _ = self.run_capture(pcapng(frames, endian, kind=kind, interface=0 if kind == 3 else 1))
                self.verify_scan(rows)
                if kind == 3:
                    self.assertEqual(rows[0]["capture_timestamp_s"], "")

    def test_sections_with_different_endian(self):
        frames = [frame(p) for p in payloads()]
        s, rows, _ = self.run_capture(pcapng(frames) + pcapng(frames, ">"))
        self.assertEqual(s["complete_scans"], 2)
        self.assertEqual(len(rows), 2880)

    def test_timestamp_resolution_and_offset(self):
        frames = [frame(p) for p in payloads()]
        _, rows, _ = self.run_capture(pcapng(frames, resolution=9, offset=3))
        self.assertEqual(rows[0]["capture_timestamp_s"], "1700003.000123456")
        _, rows, _ = self.run_capture(pcapng(frames, offset=-3))
        self.assertEqual(rows[0]["capture_timestamp_s"], "1699999997.123456000")
        _, rows, _ = self.run_capture(pcapng(frames, resolution=128 | 20))
        ticks = 1700000000123456
        ns = ticks * 1000000000 // (1 << 20)
        self.assertEqual(rows[0]["capture_timestamp_s"], f"{ns // 10**9}.{ns % 10**9:09}")

    def test_link_layers_vlan_ip_options(self):
        for link in (0, 1, 101, 113, 228, 276):
            frames = [frame(p, link=link, vlan=(link == 1), ihl=6) for p in payloads()]
            _, rows, _ = self.run_capture(pcapng(frames, link=link))
            self.verify_scan(rows)

    def test_gap_omits_incomplete_scan(self):
        packets = payloads(2)
        del packets[3]
        s, rows, _ = self.run_capture(pcapng([frame(p) for p in packets]))
        self.assertEqual((s["complete_scans"], s["incomplete_scans"]), (1, 1))
        self.verify_scan(rows, distance=1001)

    def test_prefix_and_suffix_are_not_fabricated(self):
        packets = payloads(3)[1:-1]
        s, rows, _ = self.run_capture(pcapng([frame(p) for p in packets]))
        self.assertEqual((s["complete_scans"], s["incomplete_scans"]), (1, 2))
        self.verify_scan(rows, distance=1001)

    def test_adjacent_duplicate(self):
        packets = payloads()
        packets.insert(3, packets[2])
        s, rows, _ = self.run_capture(pcapng([frame(p) for p in packets]))
        self.assertEqual(s["duplicate_packets"], 1)
        self.verify_scan(rows)

    def test_invalid_block_and_factory(self):
        for offset in (0, 1204):
            packets = payloads(2)
            bad = bytearray(packets[2]); bad[offset] ^= 1; packets[2] = bytes(bad)
            s, rows, _ = self.run_capture(pcapng([frame(p) for p in packets]))
            self.assertEqual(s["invalid_lidar_packets"], 1)
            self.verify_scan(rows, distance=1001)

    def test_fragments_are_not_decoded(self):
        frames = [frame(p, fragment=0x2000 if i == 2 else 0) for i, p in enumerate(payloads(2))]
        s, rows, _ = self.run_capture(pcapng(frames))
        self.assertEqual(s["fragmented_packets"], 1)
        self.verify_scan(rows, distance=1001)

    def test_filter_and_ambiguous_stream(self):
        packets = payloads()
        frames = [frame(p) for p in packets] + [frame(p, source=(192, 168, 8, 3)) for p in packets]
        s, _, _ = self.run_capture(pcapng(frames), expected=1)
        self.assertEqual(s["status"], "failed")
        s, rows, _ = self.run_capture(pcapng(frames), "--source-ip", "192.168.8.3")
        self.assertIn("192.168.8.3", s["stream"])
        self.verify_scan(rows)

    def test_port_override_and_no_data(self):
        data = pcapng([frame(p, port=9999) for p in payloads()])
        s, _, _ = self.run_capture(data, expected=2)
        self.assertEqual(s["status"], "no_complete_scans")
        _, rows, _ = self.run_capture(data, "--port", "9999")
        self.verify_scan(rows)

    def test_split_csv_and_no_overwrite(self):
        data = pcapng([frame(p) for p in payloads(3)])
        s, rows, destination = self.run_capture(data, "--scans-per-file", "1")
        self.assertEqual((s["csv_files"], len(rows)), (3, 4320))
        before = (destination / "summary.json").read_bytes()
        result = subprocess.run([str(EXE), str(self.root / "capture test 中文.pcapng"), str(destination), "--quiet"], capture_output=True)
        self.assertEqual(result.returncode, 1)
        self.assertEqual((destination / "summary.json").read_bytes(), before)

    def test_corrupt_capture_reports_failure(self):
        good = pcapng([frame(p) for p in payloads()])
        for bad in (good[:-1], good[:-4] + b"\0" * 4, b"BAD FILE", good[:4]):
            s, _, _ = self.run_capture(bad, expected=1)
            self.assertEqual(s["status"], "failed")

    def test_truncated_packet_report(self):
        frames = [frame(p) for p in payloads(2)]
        data = bytearray(pcap(frames))
        # Packet 3: original frame length exceeds saved length (snaplen truncation).
        position = 24 + 2 * (16 + len(frames[0]))
        struct.pack_into("<I", data, position + 12, len(frames[2]) + 10)
        s, rows, _ = self.run_capture(bytes(data))
        self.assertEqual(s["truncated_packets"], 1)
        self.verify_scan(rows, distance=1001)


if __name__ == "__main__":
    unittest.main(verbosity=2)
