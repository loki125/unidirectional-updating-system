#include "TopologicalSorter.hpp"

#include <archive.h>
#include <archive_entry.h>
#include <spdlog/spdlog.h> 


//helper func
bool ends_with(const std::string& str, const std::string& suffix) {
    if (suffix.size() > str.size()) return false;
    return std::equal(suffix.rbegin(), suffix.rend(), str.rbegin());
}

json Topologicalsorter::topo_sort(const std::string &file_name)
{
    json manifest_json = extract_manifest_JSON(file_name);
    if (manifest_json.is_null())
        throw std::runtime_error("Failed to extract manifest.json from " + file_name);

    PackageGraph pgraph = graph_builder(manifest_json);
    std::vector<int> sorted_vector = topo_sort_algo(pgraph.graph());

    return phrase_sorted_vector(pgraph, sorted_vector);
}

json Topologicalsorter::extract_manifest_JSON(const std::string &tarPath)
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

PackageGraph Topologicalsorter::graph_builder(const json &manifest_json)
{
    const json& pkg_list = manifest_json.at("Packages");

    PackageGraph pgraph(pkg_list.size());

    for(const auto& pkg_json : pkg_list){

        struct Package pkg;
        pkg.name = pkg_json.at("Package").get<std::string>();
        pkg.version = pkg_json.at("Version").get<std::string>();

        std::vector<Package>& depends = pkg.dependencies;
        const json& unprased_depends = pkg_json.at("Dependencies");//.get<std::vector<std::vector<std::string>>>();

        for(const auto& dep_vector : unprased_depends){
            struct Package dep;

            dep.name = dep_vector.at(0).get<std::string>(); //package
            dep.version = dep_vector.at(1).get<std::string>(); //version

            depends.emplace_back(dep);
        }
        pgraph.add_depend(pkg);
    }

    return pgraph;
}

std::vector<int> Topologicalsorter::topo_sort_algo(const Graph &graph)
{
    return std::vector<int>();
}

json Topologicalsorter::phrase_sorted_vector(const PackageGraph &pgraph, const std::vector<int> &sorted_vector)
{
    return json();
}
