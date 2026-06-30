#include "iosync.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void usage()
{
    std::cerr
        << "Usage:\n"
        << "  iosync devices [--verbose]\n"
        << "  iosync list [--device N] [--verbose]\n"
        << "  iosync export (--all | --id ID) --out DIR [--device N] [--sort-time] [--verbose]\n";
}

bool parse_int(const std::string& text, int& value)
{
    char* end = nullptr;
    const long parsed = std::strtol(text.c_str(), &end, 10);
    if (!end || *end != '\0' || parsed < 0) {
        return false;
    }
    value = static_cast<int>(parsed);
    return true;
}

bool parse_options(int argc, char** argv, iosync::Options& options, std::string& error)
{
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--device" && i + 1 < argc) {
            if (!parse_int(argv[++i], options.device_index)) {
                error = "--device requires a non-negative number";
                return false;
            }
        } else if (arg == "--out" && i + 1 < argc) {
            options.output_dir = argv[++i];
        } else if (arg == "--id" && i + 1 < argc) {
            options.object_id = argv[++i];
        } else if (arg == "--all") {
            options.all = true;
        } else if (arg == "--verbose" || arg == "-v") {
            options.verbose = true;
        } else if (arg == "--sort-time") {
            options.sort_time = true;
        } else {
            error = "unknown or incomplete option: " + arg;
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        usage();
        return 2;
    }

    const std::string command = argv[1];
    iosync::Options options;
    std::string error;
    if (!parse_options(argc, argv, options, error)) {
        std::cerr << "iosync: " << error << '\n';
        usage();
        return 2;
    }

    if (command == "devices") {
        const auto devices = iosync::list_devices(options, error);
        if (!error.empty()) {
            std::cerr << "iosync: " << error << '\n';
            return 1;
        }
        for (std::size_t i = 0; i < devices.size(); ++i) {
            std::cout << i << '\t' << devices[i].id << '\t' << devices[i].name << '\n';
        }
        return 0;
    }

    if (command == "list") {
        const bool ok = iosync::list_media_stream(options, [](const iosync::MediaItem& item) {
            iosync::print_media_tsv(item);
            std::cout.flush();
        }, error);
        if (!ok) {
            std::cerr << "iosync: " << error << '\n';
            return 1;
        }
        return 0;
    }

    if (command == "export") {
        if (options.output_dir.empty()) {
            std::cerr << "iosync: export requires --out DIR\n";
            usage();
            return 2;
        }
        if (!options.all && options.object_id.empty()) {
            std::cerr << "iosync: export requires --all or --id ID\n";
            usage();
            return 2;
        }
        if (options.all && !options.object_id.empty()) {
            std::cerr << "iosync: use either --all or --id, not both\n";
            usage();
            return 2;
        }
        if (!iosync::export_media(options, error)) {
            std::cerr << "iosync: " << error << '\n';
            return 1;
        }
        return 0;
    }

    usage();
    return 2;
}
