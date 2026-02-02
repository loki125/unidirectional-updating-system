#pragma once

#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <stdexcept>
#include <archive.h>
#include <archive_entry.h>
#include <nlohmann/json.hpp> 

namespace fs = std::filesystem;
using json = nlohmann::json;

class TarExtractor {
public:
    explicit TarExtractor(const fs::path& tar_file);
    json get_manifest();

private:
    json manifest_;
};
