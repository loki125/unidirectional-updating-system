#pragma once

#include <filesystem>
#include <vector>
#include <archive.h>
#include <archive_entry.h>
#include <nlohmann/json.hpp> 

namespace fs = std::filesystem;
using json = nlohmann::json;

class TarExtractor{

    json manifest_;

public:
    TarExtractor(const std::filesystem::path& tar_file);

    json get_manifest();

};