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

class TarExtractor {
public:
    explicit TarExtractor(const fs::path& tar_file);
    json get_manifest();

private:
    json manifest_;
};
