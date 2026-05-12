#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <queue>
#include <sstream>
#include <regex>
#include <fstream>
#include <iostream>
#include <memory>
#include <spawn.h>
#include <sys/wait.h>
#include <algorithm>
#include <iomanip>
#include <cstdio>
#include <stdexcept>

// Third-party headers
#include <nlohmann/json.hpp>
#include <httplib.h>
#include <openssl/sha.h>
#include <archive.h>
#include <archive_entry.h>
#include <spdlog/spdlog.h>

#include "Algorithms.hpp"
#include "PackageFactory.hpp"
#include "PackageService.hpp"
#include "utils.hpp"

class UpdateBuilder {
public:
    UpdateBuilder() = default;
    ~UpdateBuilder() = default;

    fs::path build(PackageService *service, const PackageMetadata &metadata, const std::string &f_file_path, const fs::path& broadcaster_path);

private:

    // Helper to calculate mounts 
    json _calculate_mounts(const std::string &pkg_name, const std::string &pkg_version, PackageService *service, const GSO& global_sort);

    UpdateManifest _build_update_manifest(const PackageMetadata& metadata, const std::vector<PackageMetadata>& packages, size_t total_size_byte);

    json _generate_recipe(PackageMetadata& metadata, PackageService *service, const provider_vector &provider_vector, const json &forest, const GSO& global_sort);

    fs::path _create_tar_object(const UpdateManifest& manifest, const std::vector<std::string>& file_paths, const fs::path& tar_path);

};

