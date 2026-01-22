#pragma once

#include <filesystem>
#include <vector>
#include <nlohmann/json.hpp> 

namespace TarExtractor{
    
    void extract(const std::filesystem::path& tar_file);

    json get_manifest();

    json get_main_package();

    json get_package();

}