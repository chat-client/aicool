#include "iosync.h"

#include <algorithm>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace iosync {

std::string sanitize_filename(const std::string& name)
{
    std::string out = name.empty() ? "unnamed" : name;
    const std::string bad = "\\/:*?\"<>|";
    for (char& ch : out) {
        if (static_cast<unsigned char>(ch) < 32 || bad.find(ch) != std::string::npos) {
            ch = '_';
        }
    }
    if (out == "." || out == "..") {
        out = "unnamed";
    }
    return out;
}

std::string format_size(std::uint64_t size)
{
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(size);
    std::size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < sizeof(units) / sizeof(units[0])) {
        value /= 1024.0;
        unit += 1;
    }

    std::ostringstream out;
    if (unit == 0) {
        out << size << ' ' << units[unit];
    } else {
        out << std::fixed << std::setprecision(value >= 10.0 ? 1 : 2) << value << ' ' << units[unit];
    }
    return out.str();
}

std::string format_timestamp(std::int64_t timestamp)
{
    if (timestamp <= 0) {
        return "unknown-time";
    }
    const std::time_t raw = static_cast<std::time_t>(timestamp);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &raw);
#else
    localtime_r(&raw, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return out.str();
}

void print_export_event(const std::string& action, std::size_t count, const MediaItem& item)
{
    const std::string time = item.time_text.empty() ? format_timestamp(item.timestamp) : item.time_text;
    std::cerr << "iosync: " << action << ' ' << count
              << " | time=" << time
              << " | size=" << format_size(item.size)
              << " | name=" << item.name << '\n';
}

void print_export_progress(std::size_t count, const MediaItem& item)
{
    print_export_event("exported", count, item);
}

void print_media_tsv(const MediaItem& item)
{
    std::cout << item.id << '\t'
              << item.kind << '\t'
              << item.size << '\t'
              << item.name << '\n';
}

void print_media_tsv(const std::vector<MediaItem>& items)
{
    for (const auto& item : items) {
        print_media_tsv(item);
    }
}

} // namespace iosync
