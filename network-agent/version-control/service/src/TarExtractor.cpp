#include "TarExtractor.hpp"

std::vector<std::string> split(const std::string& s, char delimiter) {
    std::vector<std::string> tokens;
    size_t start = 0;
    size_t end = s.find(delimiter);

    while (end != std::string::npos) {
        tokens.push_back(s.substr(start, end - start));
        start = end + 1;
        end = s.find(delimiter, start);
    }

    tokens.push_back(s.substr(start));
    return tokens;
}

void TarExtractor::extract(const std::filesystem::path &tar_file)
{
}

json TarExtractor::get_manifest()
{
    struct archive* a = archive_read_new();
    archive_read_support_format_tar(a);
    if(ends_with(tarPath, ".gz") || ends_with(tarPath, ".tgz"))
        archive_read_support_filter_gzip(a); 

    if (archive_read_open_filename(a, tarPath.c_str(), 10240) != ARCHIVE_OK) {
        spdlog::error("Cannot open archive: {}", archive_error_string(a));
        archive_read_free(a);
        return {};
    }

    struct archive_entry* entry;
    json manifest;

    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        std::string filename = archive_entry_pathname(entry);

        if (filename == "manifest.json") {
            size_t size = static_cast<size_t>(archive_entry_size(entry));
            std::vector<char> buffer(size);
            archive_read_data(a, buffer.data(), size);

            try {
                manifest = json::parse(buffer); // parse JSON directly
            } catch (const json::parse_error& e) {
                spdlog::error("JSON parsing error: {}", e.what());
            }

            break; // found it, stop reading
        } else {
            archive_read_data_skip(a); // skip large files efficiently
        }
    }

    archive_read_free(a);
    return manifest;
}

json TarExtractor::get_main_package()
{
    return json();
}

json TarExtractor::get_package()
{
    return json();
}
