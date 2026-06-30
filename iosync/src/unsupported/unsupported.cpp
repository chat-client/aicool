#include "iosync.h"

namespace iosync {

std::vector<Device> list_devices(const Options&, std::string& error)
{
    error = "this platform is not supported; build on macOS or Windows";
    return {};
}

std::vector<MediaItem> list_media(const Options&, std::string& error)
{
    error = "this platform is not supported; build on macOS or Windows";
    return {};
}

bool list_media_stream(const Options&, const MediaItemCallback&, std::string& error)
{
    error = "this platform is not supported; build on macOS or Windows";
    return false;
}

bool export_media(const Options&, std::string& error)
{
    error = "this platform is not supported; build on macOS or Windows";
    return false;
}

} // namespace iosync
