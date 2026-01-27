#include "TarExtractor.hpp"

TarExtractor::TarExtractor(const fs::path &tar_file)
{
    manifest_.clear();

    archive *a = archive_read_new();
    archive_read_support_filter_all(a);
    archive_read_support_format_tar(a);

    if (archive_read_open_filename(a, tar_file.c_str(), 10240) != ARCHIVE_OK) {
        throw std::runtime_error(archive_error_string(a));
    }

    archive_entry *entry = nullptr;

    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        const char *pathname = archive_entry_pathname(entry);

        if (!pathname) {
            archive_read_data_skip(a);
            continue;
        }

        std::string name = fs::path(pathname).filename().string();

        if (name == "manifest.json") {
            std::string buffer;
            buffer.resize(archive_entry_size(entry));

            ssize_t read = archive_read_data(a, buffer.data(), buffer.size());
            if (read < 0) {
                throw std::runtime_error("Failed to read manifest.json");
            }

            manifest_ = json::parse(buffer);
            break;  // we’re done
        }

        archive_read_data_skip(a);
    }

    archive_read_close(a);
    archive_read_free(a);

    if (manifest_.is_null()) {
        throw std::runtime_error("manifest.json not found in tar");
    }
}


json TarExtractor::get_manifest()
{
    return manifest_;
}

