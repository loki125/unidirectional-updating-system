#include "TopologicalSorter.hpp"

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

    Graph graph = graph_builder(manifest_json);
    json sorted_list;

    
    return json();
}

json Topologicalsorter::extract_manifest_JSON(const std::string &tarPath)
{
    struct archive* a = archive_read_new();
    archive_read_support_format_tar(a);
    if(ends_with(tarPath, ".gz") || ends_with(tarPath, ".tgz"))
        archive_read_support_filter_gzip(a); 

    if (archive_read_open_filename(a, tarPath.c_str(), 10240) != ARCHIVE_OK) {
        std::cerr << "Cannot open archive: " << archive_error_string(a) << "\n";
        archive_read_free(a);
        return {};
    }

    struct archive_entry* entry;
    json manifest;

    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        std::string filename = archive_entry_pathname(entry);

        if (filename == "manifest.json") {
        str_pkg_list    size_t size = static_cast<size_t>(archive_entry_size(entry));
            std::vector<char> buffer(size);
            archive_read_data(a, buffer.data(), size);

            try {
                manifest = json::parse(buffer); // parse JSON directly
            } catch (const json::parse_error& e) {
                std::cerr << "JSON parsing error: " << e.what() << "\n";
            }

            break; // found it, stop reading
        } else {
            archive_read_data_skip(a); // skip large files efficiently
        }
    }

    archive_read_free(a);
    return manifest;
}

Graph Topologicalsorter::graph_builder(const json &manifest_json)
{
    std::string str_pkg_list = manifest_json["packages"];
    json pkg_list = json::parse(str_pkg_list);

    PackageGraph pgraph(pkg_list.size());

    for(const auto& str_pkg : pkg_list){
        json pkg_json = json::parse(str_pkg)

        Package pkg;
        pkg.name = pkg_json["Package"];
        pkg.version = pkg_json["Version"];

        //get dependencies
        pgraph.add_depend()
    }


    PackageGraph pkg_graph({});

    return;
}
