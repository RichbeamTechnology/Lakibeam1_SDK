#include "LakiBeamCapture.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <utility>

namespace lakibeam {
namespace {
using Bytes = std::vector<std::uint8_t>;
constexpr std::size_t max_record_bytes = 64 * 1024 * 1024;

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

std::uint16_t u16(const std::uint8_t* p, bool little = true) {
    return little ? std::uint16_t(p[0] | (p[1] << 8))
                  : std::uint16_t((p[0] << 8) | p[1]);
}
std::uint32_t u32(const std::uint8_t* p, bool little = true) {
    std::uint32_t n = 0;
    for (int i = 0; i < 4; ++i) n |= std::uint32_t(p[i]) << (8 * (little ? i : 3 - i));
    return n;
}
std::uint64_t u64(const std::uint8_t* p, bool little = true) {
    std::uint64_t n = 0;
    for (int i = 0; i < 8; ++i) n |= std::uint64_t(p[i]) << (8 * (little ? i : 7 - i));
    return n;
}

Bytes read_exact(std::ifstream& f, std::size_t size, bool allow_eof = false) {
    if (size > max_record_bytes) fail("Capture record exceeds the 64 MiB safety limit.");
    Bytes b(size);
    if (size == 0) return b;
    f.read(reinterpret_cast<char*>(b.data()), static_cast<std::streamsize>(size));
    const auto got = static_cast<std::size_t>(f.gcount());
    if (got == 0 && allow_eof && f.eof()) return {};
    if (got != size) fail("Capture file ends inside a record. Re-export the complete capture.");
    return b;
}

class Decoder {
public:
    Decoder(Statistics& stats, ScanCallback callback) : stats_(stats), callback_(std::move(callback)) {}

    void gap() {
        if (!scan_.empty()) { ++stats_.incomplete_scans; discarding_ = true; }
        scan_.clear();
        step_ = 0;
    }
    void finish() { gap(); }

    bool feed(const std::uint8_t* data, std::size_t size, std::int64_t capture_time) {
        if (size != 1206 || u16(data + 1204) != 0x4037) return invalid();
        std::vector<std::size_t> blocks;
        bool padding = false;
        std::uint16_t inferred_step = 0;
        for (std::size_t i = 0; i < 12; ++i) {
            const auto* p = data + i * 100;
            if (std::all_of(p, p + 100, [](std::uint8_t c) { return c == 0xff; })) {
                padding = true;
                continue;
            }
            if (padding || u16(p) != 0xeeff || u16(p + 2) >= 36000) return invalid();
            if (!blocks.empty()) {
                const unsigned prior = u16(data + blocks.back() * 100 + 2);
                const auto delta = std::uint16_t((36000 + u16(p + 2) - prior) % 36000);
                if ((delta != 400 && delta != 160) || (inferred_step && inferred_step != delta))
                    return invalid();
                inferred_step = delta;
            }
            blocks.push_back(i);
        }
        // Both supported modes have at least two blocks even in a padded tail.
        if (blocks.size() < 2 || !inferred_step) return invalid();
        for (const auto i : blocks)
            if (u16(data + i * 100 + 2) % inferred_step != 0) return invalid();
        if (padding && u16(data + blocks.back() * 100 + 2) + inferred_step != 36000)
            return invalid();
        if (last_packet_.size() == size && std::equal(last_packet_.begin(), last_packet_.end(), data)) {
            ++stats_.duplicate_packets;
            return true;
        }
        last_packet_.assign(data, data + size);
        stats_.padding_blocks += 12 - blocks.size();
        ++stats_.lidar_packets;
        const auto timestamp = u32(data + 1200);
        for (const auto i : blocks) {
            const auto* p = data + i * 100;
            const auto angle = u16(p + 2);
            if (angle == 0) {
                gap();
                discarding_ = false;
                step_ = inferred_step;
            }
            if (!step_) {
                if (!discarding_) { ++stats_.incomplete_scans; discarding_ = true; }
                continue; // discard a partial circle at the start or after a gap
            }
            const auto expected = scan_.size() / 16 * step_;
            if (step_ != inferred_step || angle != expected) {
                ++stats_.discontinuities;
                gap();
                continue;
            }
            for (unsigned j = 0; j < 16; ++j) {
                const auto* r = p + 4 + j * 6;
                scan_.push_back(Point{std::uint16_t(angle + j * step_ / 16),
                    u16(r), r[2], u16(r + 3), r[5], timestamp, capture_time});
            }
            if (angle + step_ == 36000) {
                ++stats_.complete_scans;
                stats_.points += scan_.size();
                for (const auto& point : scan_) {
                    stats_.nonzero_first_returns += point.distance0 != 0;
                    stats_.nonzero_second_returns += point.distance1 != 0;
                }
                callback_(stats_.complete_scans, scan_);
                scan_.clear();
                step_ = 0;
                discarding_ = false;
            }
        }
        return true;
    }

private:
    bool invalid() { ++stats_.invalid_lidar_packets; gap(); return false; }
    Statistics& stats_;
    ScanCallback callback_;
    std::vector<Point> scan_;
    Bytes last_packet_;
    std::uint16_t step_ = 0;
    bool discarding_ = false;
};

std::string ip_string(const std::uint8_t* p) {
    return std::to_string(p[0]) + "." + std::to_string(p[1]) + "." +
           std::to_string(p[2]) + "." + std::to_string(p[3]);
}

class Packets {
public:
    Packets(const Options& options, Statistics& stats, Decoder& decoder)
        : options_(options), stats_(stats), decoder_(decoder) {}

    void process(const std::uint8_t* data, std::size_t caplen, std::uint32_t wirelen,
                 std::uint32_t link, std::int64_t time) {
        ++stats_.capture_packets;
        if (caplen < wirelen) { ++stats_.truncated_packets; decoder_.gap(); return; }
        std::size_t ip = 0;
        std::uint16_t protocol = 0x0800;
        if (link == 1) {
            if (caplen < 14) return short_packet();
            protocol = u16(data + 12, false); ip = 14;
            while (protocol == 0x8100 || protocol == 0x88a8 || protocol == 0x9100) {
                if (caplen < ip + 4) return short_packet();
                protocol = u16(data + ip + 2, false); ip += 4;
            }
        } else if (link == 113) {
            if (caplen < 16) return short_packet();
            protocol = u16(data + 14, false); ip = 16;
        } else if (link == 276) {
            if (caplen < 20) return short_packet();
            protocol = u16(data, false); ip = 20;
        } else if (link == 0 || link == 108) {
            if (caplen < 4) return short_packet();
            if (u32(data) != 2 && u32(data, false) != 2) return ignored();
            ip = 4;
        } else if (link != 101 && link != 228) {
            ++stats_.unsupported_link_packets; decoder_.gap(); return;
        }
        if (protocol != 0x0800) return ignored(); // IPv4 only
        if (caplen < ip + 20) return short_packet();
        if ((data[ip] >> 4) != 4) return ignored();
        const std::size_t ihl = (data[ip] & 15) * 4;
        const std::size_t total = u16(data + ip + 2, false);
        if (ihl < 20 || total < ihl || caplen < ip + total) return short_packet();
        if (data[ip + 9] != 17) return ignored();
        const auto source = ip_string(data + ip + 12);
        if (!options_.source_ip.empty() && source != options_.source_ip) return ignored();
        if (u16(data + ip + 6, false) & 0x3fff) {
            ++stats_.fragmented_packets; decoder_.gap(); return;
        }
        if (total < ihl + 8) return short_packet();
        const auto* udp = data + ip + ihl;
        if (u16(udp + 2, false) != options_.destination_port) return ignored();
        const std::size_t udp_length = u16(udp + 4, false);
        if (udp_length < 8 || udp_length > total - ihl) return short_packet();
        const auto* payload = udp + 8;
        const auto size = udp_length - 8;
        if (size != 1206 || u16(payload + 1204) != 0x4037) {
            ++stats_.invalid_lidar_packets; decoder_.gap(); return;
        }
        const auto stream = source + ":" + std::to_string(u16(udp, false)) + " -> " +
            ip_string(data + ip + 16) + ":" + std::to_string(options_.destination_port);
        if (stats_.stream.empty()) stats_.stream = stream;
        if (stats_.stream != stream)
            fail("Multiple lidar streams found. Select one with --source-ip and --port.");
        decoder_.feed(payload, size, time);
    }
private:
    void ignored() { ++stats_.ignored_packets; }
    void short_packet() { ++stats_.truncated_packets; decoder_.gap(); }
    const Options& options_;
    Statistics& stats_;
    Decoder& decoder_;
};

void classic_pcap(std::ifstream& f, const Bytes& magic, Packets& packets) {
    const bool little = magic == Bytes{0xd4, 0xc3, 0xb2, 0xa1} || magic == Bytes{0x4d, 0x3c, 0xb2, 0xa1};
    const bool nano = magic == Bytes{0x4d, 0x3c, 0xb2, 0xa1} || magic == Bytes{0xa1, 0xb2, 0x3c, 0x4d};
    auto header = read_exact(f, 20);
    if (u16(header.data(), little) != 2 || u16(header.data() + 2, little) != 4)
        fail("Unsupported PCAP version (expected 2.4).");
    const auto snap = u32(header.data() + 12, little);
    const auto link = u32(header.data() + 16, little) & 0xffff;
    while (true) {
        auto h = read_exact(f, 16, true);
        if (h.empty()) break;
        const auto cap = u32(h.data() + 8, little);
        const auto wire = u32(h.data() + 12, little);
        if (cap > wire || (snap && cap > snap)) fail("Invalid PCAP packet length.");
        const auto frac = u32(h.data() + 4, little);
        if (frac >= (nano ? 1000000000u : 1000000u)) fail("Invalid PCAP timestamp.");
        const std::int64_t ns = std::int64_t(u32(h.data(), little)) * 1000000000 +
            std::int64_t(frac) * (nano ? 1 : 1000);
        auto b = read_exact(f, cap);
        packets.process(b.data(), b.size(), wire, link, ns);
    }
}

struct Interface {
    std::uint16_t link;
    std::uint32_t snap;
    unsigned resolution = 6;
    std::int64_t offset_seconds = 0;
};

std::int64_t timestamp_ns(std::uint64_t ticks, const Interface& interface) {
    constexpr auto limit = std::uint64_t(std::numeric_limits<std::int64_t>::max());
    std::uint64_t ns = 0;
    const unsigned exponent = interface.resolution & 127;
    if (!(interface.resolution & 128)) {
        if (exponent <= 9) {
            std::uint64_t factor = 1;
            for (unsigned i = exponent; i < 9; ++i) factor *= 10;
            if (ticks > limit / factor) fail("PCAPNG timestamp is outside the supported range.");
            ns = ticks * factor;
        } else {
            ns = ticks;
            for (unsigned i = 9; i < exponent; ++i) ns /= 10;
        }
    } else {
        const auto seconds = exponent < 64 ? ticks >> exponent : 0;
        const auto remainder = exponent < 64 ? ticks & ((std::uint64_t(1) << exponent) - 1) : ticks;
        if (seconds > limit / 1000000000) fail("PCAPNG timestamp is outside the supported range.");
        ns = seconds * 1000000000 + static_cast<std::uint64_t>(
            std::floor(std::ldexp(static_cast<long double>(remainder), -static_cast<int>(exponent)) * 1000000000));
    }
    const auto offset = interface.offset_seconds;
    if (offset > 9223372036LL || offset < -9223372036LL || ns > limit)
        fail("PCAPNG timestamp is outside the supported range.");
    const auto offset_ns = offset * 1000000000;
    if ((offset_ns >= 0 && ns > limit - static_cast<std::uint64_t>(offset_ns)) ||
        (offset_ns < 0 && ns < static_cast<std::uint64_t>(-offset_ns)))
        fail("PCAPNG timestamp is outside the supported range.");
    return static_cast<std::int64_t>(ns) + offset_ns;
}

void pcapng(std::ifstream& f, Packets& packets, Decoder& decoder) {
    std::vector<Interface> interfaces;
    bool little = true;
    bool has_section = false;
    while (true) {
        const auto h = read_exact(f, 8, true);
        if (h.empty()) break;
        const bool section = std::equal(h.begin(), h.begin() + 4, Bytes{0x0a, 0x0d, 0x0d, 0x0a}.begin());
        Bytes body;
        std::uint32_t size;
        if (section) {
            auto bom = read_exact(f, 4);
            if (bom == Bytes{0x4d, 0x3c, 0x2b, 0x1a}) little = true;
            else if (bom == Bytes{0x1a, 0x2b, 0x3c, 0x4d}) little = false;
            else fail("Invalid PCAPNG byte-order magic.");
            size = u32(h.data() + 4, little);
            if (size < 28 || size % 4) fail("Invalid PCAPNG section size.");
            body = read_exact(f, size - 12);
            body.insert(body.begin(), bom.begin(), bom.end());
            if (u16(body.data() + 4, little) != 1) fail("Unsupported PCAPNG major version.");
            interfaces.clear(); decoder.gap(); has_section = true;
        } else {
            if (!has_section) fail("PCAPNG packet appears before a section header.");
            size = u32(h.data() + 4, little);
            if (size < 12 || size % 4) fail("Invalid PCAPNG block size.");
            body = read_exact(f, size - 8);
        }
        if (u32(body.data() + body.size() - 4, little) != size)
            fail("PCAPNG block length trailer does not match.");
        body.resize(body.size() - 4);
        const auto type = u32(h.data(), little);
        if (section) continue;
        if (type == 1) {
            if (body.size() < 8) fail("Truncated PCAPNG interface description.");
            Interface interface{u16(body.data(), little), u32(body.data() + 4, little)};
            for (std::size_t o = 8; o < body.size();) {
                if (body.size() - o < 4) fail("Truncated PCAPNG interface option.");
                const auto code = u16(body.data() + o, little);
                const std::size_t len = u16(body.data() + o + 2, little);
                o += 4;
                if (len > body.size() - o || ((len + 3) & ~std::size_t(3)) > body.size() - o)
                    fail("Invalid PCAPNG interface option length.");
                if (code == 0) { if (len != 0) fail("Invalid end-of-options field."); break; }
                if (code == 9) {
                    if (len != 1) fail("Invalid timestamp-resolution option.");
                    interface.resolution = body[o];
                } else if (code == 14) {
                    if (len != 8) fail("Invalid timestamp-offset option.");
                    const auto bits = u64(body.data() + o, little);
                    if (bits == (std::uint64_t(1) << 63)) fail("Timestamp offset is outside the supported range.");
                    interface.offset_seconds = (bits >> 63) ? -static_cast<std::int64_t>((~bits) + 1) : static_cast<std::int64_t>(bits);
                }
                o += (len + 3) & ~std::size_t(3);
            }
            interfaces.push_back(interface);
        } else if (type == 6 || type == 2) {
            if (body.size() < 20) fail("Truncated PCAPNG packet header.");
            const auto id = type == 6 ? u32(body.data(), little) : u16(body.data(), little);
            if (id >= interfaces.size()) fail("PCAPNG references an unknown interface.");
            const auto& interface = interfaces[id];
            const auto ticks = (std::uint64_t(u32(body.data() + 4, little)) << 32) | u32(body.data() + 8, little);
            const std::size_t cap = u32(body.data() + 12, little);
            const auto wire = u32(body.data() + 16, little);
            if (cap > wire || cap > body.size() - 20 || ((cap + 3) & ~std::size_t(3)) > body.size() - 20 ||
                (interface.snap && cap > interface.snap)) fail("Invalid PCAPNG captured packet length.");
            packets.process(body.data() + 20, cap, wire, interface.link, timestamp_ns(ticks, interface));
        } else if (type == 3) {
            if (interfaces.empty() || body.size() < 4) fail("Invalid PCAPNG simple packet.");
            const auto& interface = interfaces.front();
            const auto wire = u32(body.data(), little);
            const std::size_t cap = interface.snap ? std::min(wire, interface.snap) : wire;
            if (cap > body.size() - 4 || ((cap + 3) & ~std::size_t(3)) != body.size() - 4)
                fail("Invalid PCAPNG simple packet length.");
            packets.process(body.data() + 4, cap, wire, interface.link, -1);
        }
    }
}
} // namespace

Statistics read_capture(const std::filesystem::path& file,
                        const Options& options, const ScanCallback& on_scan) {
    std::ifstream input(file, std::ios::binary);
    if (!input) fail("Cannot open the capture file.");
    if (!on_scan) fail("A scan callback is required.");
    Statistics stats;
    Decoder decoder(stats, on_scan);
    Packets packets(options, stats, decoder);
    const auto magic = read_exact(input, 4);
    if (magic == Bytes{0x0a, 0x0d, 0x0d, 0x0a}) {
        input.seekg(0);
        pcapng(input, packets, decoder);
    } else if (magic == Bytes{0xd4, 0xc3, 0xb2, 0xa1} || magic == Bytes{0xa1, 0xb2, 0xc3, 0xd4} ||
               magic == Bytes{0x4d, 0x3c, 0xb2, 0xa1} || magic == Bytes{0xa1, 0xb2, 0x3c, 0x4d}) {
        classic_pcap(input, magic, packets);
    } else fail("This is not a PCAP or PCAPNG capture file.");
    decoder.finish();
    return stats;
}
} // namespace lakibeam
