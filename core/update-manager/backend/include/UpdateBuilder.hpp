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

/**
 * @brief Constructs the final software update package containing binaries, metadata, and recipes.
 */
class UpdateBuilder {
public:
    UpdateBuilder() = default;
    ~UpdateBuilder() = default;

    /**
     * @brief Orchestrates dependency resolution, graph sorting, recipe generation, and tarball creation.
     * @param service Pointer to the underlying package service (e.g., DebianPackageService).
     * @param metadata The root package metadata.
     * @param f_file_path The local path to the root package file.
     * @param broadcaster_path Directory where the final tarball should be saved.
     * @return fs::path The absolute path to the generated tarball update file.
     */
    fs::path build(PackageService *service, const PackageMetadata &metadata, const std::string &f_file_path, const fs::path& broadcaster_path);

private:

    /**
     * @brief Determines runtime mount requirements using the dependency graph.
     * @param pkg_name The package name.
     * @param pkg_version The package version.
     * @param service The package service instance.
     * @param global_sort Graph Sort Object providing dependency ordering.
     * @return json JSON array of required paths to mount.
     */
    json _calculate_mounts(const std::string &pkg_name, const std::string &pkg_version, PackageService *service, const GSO& global_sort);

    /**
     * @brief Creates the global manifest containing metadata for all packages in the update.
     * @param metadata Root package metadata.
     * @param packages List of all resolved package metadata objects.
     * @param total_size_byte Cumulative size of all files in the update.
     * @return UpdateManifest Structured manifest object.
     */
    UpdateManifest _build_update_manifest(const PackageMetadata& metadata, const std::vector<PackageMetadata>& packages, size_t total_size_byte);

    /**
     * @brief Generates execution and mounting instructions (recipe) for a specific package.
     * @param metadata The package metadata.
     * @param service The package service instance.
     * @param provider_vector List of files/executables provided by this package.
     * @param forest Map representing symbolic links required by this package.
     * @param global_sort Graph Sort Object providing dependency ordering.
     * @return json JSON representation of the runtime recipe.
     */
    json _generate_recipe(PackageMetadata& metadata, PackageService *service, const provider_vector &provider_vector, const json &forest, const GSO& global_sort);

    /**
     * @brief Bundles all package files and the manifest into a single `.tar` archive.
     * @param manifest The generated update manifest.
     * @param file_paths List of all file paths to include in the archive.
     * @param tar_path Output directory for the tarball.
     * @return fs::path Path to the created tar archive.
     */
    fs::path _create_tar_object(const UpdateManifest& manifest, const std::vector<std::string>& file_paths, const fs::path& tar_path);

};