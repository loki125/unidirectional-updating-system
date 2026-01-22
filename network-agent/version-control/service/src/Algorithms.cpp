#include "Algorithms.hpp"

#include <archive.h>
#include <archive_entry.h>
#include <queue>
#include <spdlog/spdlog.h> 


//helper func
bool ends_with(const std::string& str, const std::string& suffix) {
    if (suffix.size() > str.size()) return false;
    return std::equal(suffix.rbegin(), suffix.rend(), str.rbegin());
}

json RTS::sort(const json &manifest_json)
{
    if (manifest_json.is_null())
        throw std::runtime_error("Failed to extract manifest.json");

    package_json["manifest"] = manifest_json;
    PackageGraph pgraph = graph_builder(manifest_json);
    std::vector<int> sorted_vector = sort_algo(pgraph.graph());

    package_json["vector"] = phrase_sorted_vector(pgraph, sorted_vector);
    return package_json;
}



PackageGraph RTS::graph_builder(const json &manifest_json)
{
    const json& pkg_list = manifest_json.at("Packages");

    PackageGraph pgraph(pkg_list.size());

    for(const auto& pkg_json : pkg_list){

        struct Package pkg;
        pkg.name = pkg_json.at("Package").get<std::string>();
        pkg.version = pkg_json.at("Version").get<std::string>();
        pkg.package_path = pkg_json.at("Filename").get<std::string>();

        std::vector<Package>& depends = pkg.dependencies;
        const json& unprased_depends = pkg_json.at("Dependencies");

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

std::vector<int> RTS::sort_algo(const Graph &graph){
    /*
    Reverse Topological Sort Algorithm Explanation:
    1. create G' as G reversed
    2. start from the "leafs" of G (nodes with 0 out-degree in G)
    3. BFS Traversal:
        - Ask each leaf who are their neighbors (in G')
        - For each neighbor:
            - Look at his own neighbors in G and ask if all of them are already in the list
            - If yes then add him to the list
            - Add to queue to "go up to the next level"
            - if BFS Traversal repeted more then the graphs size break *circular dependency detected*
    4. Repeat until no more nodes can be added
    5. if there was a cycle
        - update JSON error handling here, graph is not DAG
    6. return the list
    */
    std::size_t n = graph.size();
    

    std::vector<std::vector<std::size_t>> adj_rev(n); // This is G'
    std::vector<int> out_degree_g(n, 0); // Represents dependency count

    for (std::size_t u = 0; u < n; ++u) {
        const auto& neighbors = graph.neighbors(u);
        out_degree_g[u] = neighbors.size(); // Count neighbors in G
        
        for (std::size_t v : neighbors) {
            // Original: u -> v
            // Reverse (G'): v -> u
            adj_rev[v].push_back(u);
        }
    }

    std::queue<std::size_t> q;
    std::vector<int> result; // The list

    for (std::size_t i = 0; i < n; ++i) {
        if (out_degree_g[i] == 0) {
            q.push(i);
            result.push_back(static_cast<int>(i));
        }
    }

    // BFS Traversal
    while (!q.empty()) {
        std::size_t curr = q.front();
        q.pop();

        for (std::size_t neighbor : adj_rev[curr]) {
            
            out_degree_g[neighbor]--;

            if (out_degree_g[neighbor] == 0) {
                // "If all neighbors in list then add him"
                result.push_back(static_cast<int>(neighbor));
                
                // Add to queue to "go up to the next level"
                q.push(neighbor);
            }
        }
    }

    //Check if the graph had a cycle (if result size < n)
    if (result.size() != n) {
        // setting up JSON error handling here, graph is not DAG
    }
    return result;
}

json RTS::phrase_sorted_vector(const PackageGraph &pgraph, const std::vector<int> &sorted_vector)
{
    json sorted_json = json::array();
    for (const auto& index : sorted_vector) {
        sorted_json.push_back(pgraph.get_package(index).package_path);
    }
    return sorted_json;
}