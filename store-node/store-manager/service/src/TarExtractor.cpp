#include "TarExtractor.hpp"

TarExtractor::TarExtractor(const fs::path& tar_path)
{
    manifest_.clear();

    archive* in = archive_read_new();
    archive_read_support_filter_all(in);
    archive_read_support_format_tar(in);

    if (archive_read_open_filename(in, tar_path.c_str(), 10240) != ARCHIVE_OK) {
        throw std::runtime_error(archive_error_string(in));
    }

    archive_entry* entry = nullptr;
    fs::path output_dir = tar_path.parent_path();

    while (archive_read_next_header(in, &entry) == ARCHIVE_OK) {
        const char* pathname = archive_entry_pathname(entry);
        if (!pathname) {
            archive_read_data_skip(in);
            continue;
        }

        std::string filename = fs::path(pathname).filename().string();

        // get manifest.json
        if (filename == "manifest.json") {
            std::string buffer;
            buffer.resize(archive_entry_size(entry));

            ssize_t r = archive_read_data(in, buffer.data(), buffer.size());
            if (r < 0) {
                throw std::runtime_error("Failed to read manifest.json");
            }

            manifest_ = json::parse(buffer);
            continue;
        }

        // extract any other file
        fs::path out_path = output_dir / filename;

        std::ofstream out(out_path, std::ios::binary);
        if (!out) {
            throw std::runtime_error("Failed to create " + out_path.string());
        }

        char buffer[8192];
        ssize_t size;
        while ((size = archive_read_data(in, buffer, sizeof(buffer))) > 0) {
            out.write(buffer, size);
        }

        if (size < 0) {
            throw std::runtime_error("Failed to extract " + filename);
        }

        out.close();
    }

    archive_read_close(in);
    archive_read_free(in);

    if (manifest_.is_null()) {
        throw std::runtime_error("manifest.json not found in tar");
    }
}

json TarExtractor::get_manifest()
{
    return manifest_;
}

