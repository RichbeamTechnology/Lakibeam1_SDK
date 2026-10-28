# LakiBeam1 offline capture SDK

`offline/LakiBeamCapture.h` provides a streaming callback API for saved captures.
It has no Boost, libpcap, Npcap or Python runtime dependency. It is an additional
entry point; the original `Data SDK/` and `Config SDK/` live APIs are unchanged.

```cpp
#include "LakiBeamCapture.h"
#include <iostream>

int main() {
    lakibeam::Options options; // destination_port = 2368; source_ip = auto
    auto stats = lakibeam::read_capture("capture.pcapng", options,
        [](std::uint64_t scan_id, const std::vector<lakibeam::Point>& scan) {
            std::cout << scan_id << ": " << scan.size() << " positions\n";
            // Copy anything that must outlive this callback. The vector is reused.
        });
    std::cout << stats.complete_scans << " complete scans\n";
}
```

Compile both your source and `offline/LakiBeamCapture.cpp` with C++17. Catch
`std::exception` at the application boundary. The callback runs synchronously;
an exception aborts parsing and propagates to the caller. Captures are read
sequentially with a 64 MiB maximum record/block size and one scan in memory.

## Build the customer executable

Windows: install Visual Studio 2019 or later with Desktop development with C++
and CMake. In a developer terminal:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\build\Release\LakiBeamCapture.exe "capture.pcapng" --quiet
```

Linux/macOS:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/LakiBeamCapture capture.pcapng --quiet
```

Python 3 is needed only to run tests and create packages. Direct GCC build:

```sh
g++ -std=c++17 -O2 offline/LakiBeamCapture.cpp offline/main.cpp -o LakiBeamCapture
python3 tests/test_capture.py ./LakiBeamCapture
```

## Decoding contract

- LakiBeam1 1206-byte UDP payload; wire-order factory bytes `37 40`.
- Valid blocks have `FF EE` headers; trailing all-FF blocks are padding.
- Within-packet angular increments must be consistently 400 or 160 raw units.
  Point angles are in hundredths of a degree, spaced by 25 or 10 respectively.
- Start assembling at angle zero. Require consecutive block angles through
  the end of the circle. Emit immediately when the last valid block arrives,
  so a complete EOF circle does not need another packet or a fake sentinel.
- Missing, reordered or invalid data cannot silently bridge a scan gap. Discard
  the incomplete circle and resume at a valid zero-angle block. Adjacent exact
  duplicate payloads are ignored. Non-adjacent duplicate/out-of-order packets
  are treated conservatively as discontinuities.
- Both return distances and intensities are preserved. Zero distance positions
  remain in the scan. Raw distance scale is not guessed. Packet timestamps are
  retained without per-point interpolation or conversion of lidar time units.
- All matching packets must belong to one IPv4 UDP tuple. Use Options filters
  when a file contains several lidar streams. No IP fragment reassembly.
- Metadata errors (bad block lengths, unknown interfaces, invalid timestamps)
  fail the entire operation. Packet-level invalid/truncated/fragmented data is
  counted and skipped. Check Statistics and CLI summary.json before analysis.
- `incomplete_scans` counts discarded partial scan segments, including a partial
  prefix/suffix; it is not an estimate of how many entire revolutions were lost.
- PCAPNG timestamps below nanosecond resolution are truncated to nanoseconds.
  Simple Packet Blocks have `capture_time_ns = -1`.

## Why a dedicated offline decoder

The live SDK processes pairs of UDP packets and uses fixed output ring buffers.
That is not suitable for exhaustive file export. The offline API uses complete
scan callbacks and handles padding and EOF explicitly, avoiding the first-circle
rejection and tail-angle interpolation problem found in the original parser.
Applications using the original live class must switch to this offline entry
point to receive these offline fixes; this change does not rewrite the live API.

## Validation

`tests/test_capture.py` builds synthetic fixtures and checks both return fields,
angles, first/last scans, partial scans, dropped packets, duplicates, endianness,
timestamp resolution, PCAPNG sections/interfaces/packet blocks, Ethernet VLAN,
IPv4 options, raw IP, Linux cooked captures, loopback, errors and no-overwrite.
The Windows/Linux CI runs the same tests before publishing a build artifact.
Customer capture files must not be committed to the repository.
