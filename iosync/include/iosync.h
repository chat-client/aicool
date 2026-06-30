#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace iosync {

struct Device {
    std::string id;
    std::string name;
};

struct MediaItem {
    std::string id;
    std::string name;
    std::string kind;
    std::uint64_t size = 0;
    std::int64_t timestamp = 0;
    std::string time_text;
};

struct Options {
    int device_index = 0;
    std::string output_dir;
    std::string object_id;
    bool all = false;
    bool verbose = false;
    bool sort_time = false;
};

using MediaItemCallback = std::function<void(const MediaItem&)>;

std::vector<Device> list_devices(const Options& options, std::string& error);
std::vector<MediaItem> list_media(const Options& options, std::string& error);
bool list_media_stream(const Options& options, const MediaItemCallback& callback, std::string& error);
bool export_media(const Options& options, std::string& error);

std::string sanitize_filename(const std::string& name);
std::string format_size(std::uint64_t size);
std::string format_timestamp(std::int64_t timestamp);
void print_export_event(const std::string& action, std::size_t count, const MediaItem& item);
void print_export_progress(std::size_t count, const MediaItem& item);
void print_media_tsv(const MediaItem& item);
void print_media_tsv(const std::vector<MediaItem>& items);

} // namespace iosync
