#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace lakibeam {

// All distance and intensity fields are copied without unit conversion.
struct Point {
    std::uint16_t angle;       // hundredths of a degree
    std::uint16_t distance0;
    std::uint8_t rssi0;
    std::uint16_t distance1;
    std::uint8_t rssi1;
    std::uint32_t lidar_timestamp; // original packet timestamp, not per-point time
    std::int64_t capture_time_ns;  // -1 when absent in the capture
};

struct Statistics {
    std::uint64_t capture_packets = 0;
    std::uint64_t lidar_packets = 0;
    std::uint64_t ignored_packets = 0;
    std::uint64_t truncated_packets = 0;
    std::uint64_t unsupported_link_packets = 0;
    std::uint64_t fragmented_packets = 0;
    std::uint64_t invalid_lidar_packets = 0;
    std::uint64_t duplicate_packets = 0;
    std::uint64_t padding_blocks = 0;
    std::uint64_t complete_scans = 0;
    std::uint64_t incomplete_scans = 0;
    std::uint64_t discontinuities = 0;
    std::uint64_t points = 0;
    std::uint64_t nonzero_first_returns = 0;
    std::uint64_t nonzero_second_returns = 0;
    std::string stream;
};

struct Options {
    std::uint16_t destination_port = 2368;
    std::string source_ip;  // empty: detect exactly one matching lidar stream
};

using ScanCallback = std::function<void(std::uint64_t, const std::vector<Point>&)>;

// Streaming, bounded-memory PCAP / PCAPNG reader. Throws std::runtime_error on
// malformed capture structure, ambiguous lidar streams, or callback failure.
// Emits complete 1440 / 3600 point circles, including a complete circle at EOF.
// Incomplete circles are counted and omitted, never filled with invented points.
Statistics read_capture(const std::filesystem::path& file,
                        const Options& options, const ScanCallback& on_scan);

} // namespace lakibeam
