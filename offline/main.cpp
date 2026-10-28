#include "LakiBeamCapture.h"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <locale>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#endif

namespace fs = std::filesystem;

std::string json_string(const std::string& s) {
    std::ostringstream out;
    out << '"';
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') out << '\\' << c;
        else if (c < 32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c);
        else out << c;
    }
    out << '"';
    return out.str();
}

void write_summary(const fs::path& directory, const lakibeam::Statistics& s,
                   std::uint64_t files, const std::string& status, const std::string& error = {}) {
    std::ofstream out(directory / "summary.json", std::ios::binary);
    out.exceptions(std::ios::badbit | std::ios::failbit);
    out << "{\n  \"tool_version\": \"1.0.0\",\n  \"status\": " << json_string(status)
        << ",\n  \"error\": " << json_string(error) << ",\n  \"stream\": " << json_string(s.stream);
#define FIELD(name) out << ",\n  \"" #name "\": " << s.name
    FIELD(capture_packets); FIELD(lidar_packets); FIELD(ignored_packets);
    FIELD(truncated_packets); FIELD(unsupported_link_packets); FIELD(fragmented_packets);
    FIELD(invalid_lidar_packets); FIELD(duplicate_packets); FIELD(padding_blocks);
    FIELD(complete_scans); FIELD(incomplete_scans); FIELD(discontinuities); FIELD(points);
    FIELD(nonzero_first_returns); FIELD(nonzero_second_returns);
#undef FIELD
    out << ",\n  \"csv_files\": " << files << "\n}\n";
    out.close();
}

class CsvWriter {
public:
    CsvWriter(fs::path directory, unsigned per_file)
        : directory_(std::move(directory)), per_file_(per_file) {}

    void scan(std::uint64_t id, const std::vector<lakibeam::Point>& points) {
        if ((id - 1) % per_file_ == 0) {
            if (out_.is_open()) out_.close();
            std::ostringstream name;
            name << "points_" << std::setfill('0') << std::setw(4) << ++files_ << ".csv";
            out_.exceptions(std::ios::badbit | std::ios::failbit);
            out_.open(directory_ / name.str(), std::ios::binary);
            out_.imbue(std::locale::classic());
            out_ << "scan_id,point_index,angle_deg,distance0_raw,rssi0,distance1_raw,rssi1,lidar_packet_timestamp_raw,capture_timestamp_s\n";
        }
        for (std::size_t i = 0; i < points.size(); ++i) {
            const auto& p = points[i];
            out_ << id << ',' << i << ',' << p.angle / 100 << '.'
                 << std::setfill('0') << std::setw(2) << p.angle % 100 << ','
                 << p.distance0 << ',' << unsigned(p.rssi0) << ',' << p.distance1 << ','
                 << unsigned(p.rssi1) << ',' << p.lidar_timestamp << ',';
            if (p.capture_time_ns >= 0)
                out_ << p.capture_time_ns / 1000000000 << '.' << std::setw(9) << p.capture_time_ns % 1000000000;
            out_ << '\n';
        }
    }
    void close() { if (out_.is_open()) out_.close(); }
    std::uint64_t files() const { return files_; }
private:
    fs::path directory_;
    unsigned per_file_;
    std::uint64_t files_ = 0;
    std::ofstream out_;
};

void usage() {
    std::cout << "LakiBeam1 Capture Exporter 1.0.0\n"
        "Usage: LakiBeamCapture INPUT.pcapng [OUTPUT_FOLDER] [options]\n"
        "Options:\n  --port N               UDP destination port (default 2368)\n"
        "  --source-ip A.B.C.D     Select one lidar in a mixed capture\n"
        "  --scans-per-file N     1..200 complete scans per CSV (default 200)\n"
        "  --quiet                Do not show Windows result dialogs\n"
        "  --help                 Show this help\n"
        "Supports LakiBeam1 1206-byte / 0x4037, 1440 or 3600 points per circle.\n";
}

unsigned number(const std::string& text, unsigned max) {
    if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
        throw std::runtime_error("Expected a positive integer option value.");
    const auto n = std::stoull(text);
    if (n == 0 || n > max) throw std::runtime_error("Numeric option is outside the allowed range.");
    return static_cast<unsigned>(n);
}

#ifdef _WIN32
fs::path choose_file() {
    std::vector<wchar_t> buffer(32768, L'\0');
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    dialog.lpstrFilter = L"Wireshark captures (*.pcap;*.pcapng)\0*.pcap;*.pcapng\0All files\0*.*\0";
    dialog.lpstrTitle = L"LakiBeam1 - Select a capture file";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog)) {
        if (CommDlgExtendedError()) throw std::runtime_error("The Windows file picker could not open.");
        return {};
    }
    return fs::path(buffer.data());
}
std::wstring wide_utf8(const std::string& text) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), n);
    return result;
}
#endif

int run(const std::vector<std::string>& args) {
    bool quiet = false;
    for (const auto& a : args) if (a == "--quiet") quiet = true;
    fs::path output;
    bool created_output = false;
    try {
        lakibeam::Options options;
        unsigned per_file = 200;
        std::vector<fs::path> positional;
        for (std::size_t i = 1; i < args.size(); ++i) {
            const auto& a = args[i];
            if (a == "--help" || a == "-h") { usage(); return 0; }
            if (a == "--quiet") continue;
            if (a == "--port" || a == "--source-ip" || a == "--scans-per-file") {
                if (++i == args.size()) throw std::runtime_error("Missing value for " + a);
                if (a == "--port") options.destination_port = static_cast<std::uint16_t>(number(args[i], 65535));
                else if (a == "--scans-per-file") per_file = number(args[i], 200);
                else options.source_ip = args[i];
            } else if (a.rfind("--", 0) == 0) throw std::runtime_error("Unknown option: " + a);
            else positional.push_back(fs::u8path(a));
        }
        if (positional.size() > 2) throw std::runtime_error("Expected one input file and an optional output folder.");
        fs::path input;
        if (positional.empty()) {
#ifdef _WIN32
            if (quiet) { usage(); return 2; }
            input = choose_file();
            if (input.empty()) return 0;
#else
            usage(); return 2;
#endif
        } else input = positional[0];
        if (!fs::is_regular_file(input)) throw std::runtime_error("Input is not an existing file.");
        if (positional.size() == 2) {
            output = positional[1];
            if (fs::exists(output)) throw std::runtime_error("Output folder already exists. Choose a new folder.");
        } else {
            output = input.parent_path() / (input.stem().u8string() + "_decoded");
            for (unsigned suffix = 2; fs::exists(output); ++suffix)
                output = input.parent_path() / (input.stem().u8string() + "_decoded_" + std::to_string(suffix));
        }
        // create_directory atomically refuses a competing/existing destination.
        if (!fs::create_directory(output)) throw std::runtime_error("Cannot create a new output folder.");
        created_output = true;
        CsvWriter writer(output, per_file);
        const auto stats = lakibeam::read_capture(input, options,
            [&writer](std::uint64_t id, const std::vector<lakibeam::Point>& points) { writer.scan(id, points); });
        writer.close();
        const bool success = stats.complete_scans != 0;
        write_summary(output, stats, writer.files(), success ? "complete" : "no_complete_scans");
        if (!success) {
            const std::string message = "No complete LakiBeam1 scans found. Check --port, --source-ip, capture length and device mode. See summary.json.";
            std::cerr << message << '\n';
#ifdef _WIN32
            if (!quiet) MessageBoxW(nullptr, wide_utf8(message + "\n\n" + fs::absolute(output).u8string()).c_str(), L"LakiBeam1 Capture Exporter", MB_OK | MB_ICONWARNING);
#endif
            return 2;
        }
        std::ostringstream message;
        message << "Export complete.\nScans: " << stats.complete_scans << "\nPoints: " << stats.points
                << "\nCSV files: " << writer.files() << "\nIncomplete scans omitted: " << stats.incomplete_scans
                << "\nInvalid/truncated/fragmented packets: "
                << stats.invalid_lidar_packets + stats.truncated_packets + stats.fragmented_packets
                << "\n\nOutput: " << fs::absolute(output).u8string();
        std::cout << message.str() << '\n';
#ifdef _WIN32
        if (!quiet) MessageBoxW(nullptr, wide_utf8(message.str()).c_str(), L"LakiBeam1 Capture Exporter", MB_OK | MB_ICONINFORMATION);
#else
        (void)quiet;
#endif
        return 0;
    } catch (const std::exception& error) {
        if (created_output) {
            try { write_summary(output, {}, 0, "failed", error.what()); } catch (...) {}
        }
        std::string message = error.what();
        if (created_output) message += "\nPartial output is not a successful export. See " + (output / "summary.json").u8string();
        std::cerr << "Error: " << message << '\n';
#ifdef _WIN32
        if (!quiet) MessageBoxW(nullptr, wide_utf8(message).c_str(), L"LakiBeam1 Capture Exporter - Error", MB_OK | MB_ICONERROR);
#endif
        return 1;
    }
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) args.push_back(fs::path(argv[i]).u8string());
    return run(args);
}
#else
int main(int argc, char** argv) { return run(std::vector<std::string>(argv, argv + argc)); }
#endif
