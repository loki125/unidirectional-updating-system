#pragma once

#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <stdexcept>
#include <archive.h>
#include <archive_entry.h>
#include <nlohmann/json.hpp> 

#include "utils.hpp"

/**
 * @brief Utility for extracting tar archives and parsing manifests.
 */
class TarExtractor {
public:
    /**
     * @brief Extracts archive files and parses manifest.json into memory.
     * @param tar_file Filesystem path to the tar archive.
     */
    explicit TarExtractor(const fs::path& tar_file);

    /**
     * @brief Retrieves the parsed manifest JSON.
     * @return json The manifest data.
     */
    json get_manifest();

private:
    json manifest_; ///< Internal cache of the parsed manifest.
};