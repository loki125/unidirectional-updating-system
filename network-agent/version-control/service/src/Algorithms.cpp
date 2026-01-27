#include "Algorithms.hpp"

#include <queue>
#include <stack>
#include <functional>
#include <spdlog/spdlog.h> 

std::vector<json> RTS::sort(const json &manifest_json)
{  
    if (manifest_json.is_null())
        throw std::runtime_error("Failed to extract manifest.json");

    PackageGraph pgraph = graph_builder(manifest_json);
    Graph& graph = pgraph.graph();

    //Detect and Break Cycles
    resolve_circular_dependencies(graph);
    std::vector<int> sorted_vector = sort_algo(graph);

    return phrase_sorted_vector(pgraph, sorted_vector);
}

PackageGraph RTS::graph_builder(const json &manifest_json)
{
    const std::vector<json>& pkg_list = manifest_json.at("Packages").get<std::vector<json>>();

    PackageGraph pgraph(pkg_list.size());

    for(const auto& pkg_json : pkg_list){
        struct Package pkg(pkg_json);

        std::vector<Package>& depends = pkg.dependencies;
        const json& unprased_depends = pkg_json.at("Dependencies");

        for(const auto& dep_vector : unprased_depends){
            struct Package dep{};

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
    
    Graph graph_rev(n); // This is G'
    std::vector<int> out_degree_g(n, 0); // Represents dependency count

    for (std::size_t u = 0; u < n; ++u) {
        const auto& neighbors = graph.neighbors(u);
        out_degree_g[u] = neighbors.size(); // Count neighbors in G
        
        for (std::size_t v : neighbors) {
            // Original: u -> v
            // Reverse (G'): v -> u
            graph_rev.add_edge(v, u);
        }
    }

    std::queue<std::size_t> q;
    std::vector<int> result; // The list

    //insert all leafs (0 out-degree) into the queue
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

        for (std::size_t neighbor : graph_rev.neighbors(curr)) {
            
            out_degree_g[neighbor]--;

            if (out_degree_g[neighbor] == 0) {
                // "If all neighbors in list then add him"
                result.push_back(static_cast<int>(neighbor));
                
                // Add to queue to "go up to the next level"
                q.push(neighbor);
            }
            
        }
    }

    if (result.size() != n) 
        throw std::runtime_error("Circular dependency detected"); // circular dependency detected

    spdlog::info("[RTS] Topological Sort Result: {}\n", fmt::join(result, ", "));
    return result;
}

void RTS::resolve_circular_dependencies(Graph &graph){
    /* 
    Using Tarjan's Algorithm for Strongly Connected Components to detect cycles.
       by each cycle if size == 2. Soft conflict, remove one direction.
       if size > 2. Hard conflict, throw runtime error.
    Explanation:
        1. Perform DFS and track discovery times and low-link values.
        2. When we find an SCC (low[u] == disc[u]), we check its size. 
              - If size == 2, we log a warning and remove one edge to break the cycle.
              - If size > 2, we log an error indicating a complex cycle.
    */

    std::size_t n = graph.size();
    
    // Tarjan's Variables
    std::vector<int> disc(n, -1); // Discovery time
    std::vector<int> low(n, -1);  // Low link value
    std::vector<bool> stackMember(n, false);
    std::stack<std::size_t> st;
    int timer = 0;

    // store edges to remove 
    std::vector<std::pair<std::size_t, std::size_t>> edges_to_cut;

    // DFS Lambda
    std::function<void(std::size_t)> SCC_DFS = [&](std::size_t u) {
        disc[u] = low[u] = ++timer;
        st.push(u);
        stackMember[u] = true;

        for (std::size_t v : graph.neighbors(u)) {
            if (disc[v] == -1) {
                // If v is not visited, recurse
                SCC_DFS(v);
                low[u] = std::min(low[u], low[v]);
            } else if (stackMember[v]) {
                // If v is in stack, it's a back-edge (part of SCC)
                low[u] = std::min(low[u], disc[v]);
            }
        }

        // If u is the head of an SCC
        if (low[u] == disc[u]) {
            std::vector<std::size_t> component;
            while (true) {
                std::size_t v = st.top();
                st.pop();
                stackMember[v] = false;
                component.push_back(v);
                if (u == v) break;
            }

            // detection
            if (component.size() == 2) {
                std::size_t nodeA = component[0], nodeB = component[1];

                spdlog::warn("[Cycle Breaker] Size 2 Cycle Detected: ID {} <--> ID {}", nodeA, nodeB);
                edges_to_cut.push_back({nodeA, nodeB});
            }
            else if (component.size() > 2) 
                throw std::runtime_error("[RTS] [Cycle Breaker] Complex Cycle (Size {}) Detected! IDs: {}", 
                              component.size(), fmt::join(component, ", "));
        }
    };

    // handles disconnected graphs
    for (std::size_t i = 0; i < n; i++) {
        if (disc[i] == -1) {
            SCC_DFS(i);
        }
    }

    // excecute edge removals
    for (const auto& edge : edges_to_cut) {
        spdlog::info("[Cycle Breaker] Severing dependency: {} -> {}", edge.first, edge.second);
        graph.rm_edge(edge.first, edge.second);
    }

}

std::vector<json> RTS::phrase_sorted_vector(const PackageGraph &pgraph, const std::vector<int> &sorted_vector)
{
    std::vector<json> sorted_packages;
    for (const auto& index : sorted_vector) {
        sorted_packages.push_back(pgraph.get_package(index).package_json);
        spdlog::info("\n[RTS] Sorted Package: {}\n", pgraph.get_package(index).package_json.dump());
    }
    return sorted_packages;
}

/*







this is just so i know where the implementation of RTS ends and SLF starts











*/
void SLF::build_slf(const json &package)
{
    try {
        std::string pkg_type = package.at("Type");

        std::string store_path = package.at("Store_path");
        std::string deb_file = package.at("FileName");

        if (pkg_type == "Debian")     
            deb_inspector(store_path, deb_file);
        
        else {
            spdlog::error("[SLF] Unknown package type: {}", pkg_type);
        }
    } catch (const json::exception& e) {
        spdlog::error("[SLF] JSON Error: {}", e.what());
    } catch (const std::exception& e) {
        spdlog::error("[SLF] Error: {}", e.what());
    }
}

bool SLF::is_executable(const fs::path& p) {
    auto perms = fs::status(p).permissions();
    return (perms & fs::perms::owner_exec) != fs::perms::none ||
            (perms & fs::perms::group_exec) != fs::perms::none ||
            (perms & fs::perms::others_exec) != fs::perms::none;
}

void SLF::deb_inspector(const fs::path& store_path, const std::string& deb_filename) {
    
    fs::path full_deb_path = store_path / deb_filename;

    if (!fs::exists(full_deb_path)) {
        spdlog::error("[SLF] Error: .deb file not found at {}", full_deb_path.string());
        return;
    }

    // Prepare Temp Extraction Directory
    fs::path temp_dir = store_path / "temp_slf_extraction";
    if (fs::exists(temp_dir)) fs::remove_all(temp_dir);
    fs::create_directory(temp_dir);

    spdlog::info("[SLF] Extracting to temp dir...");

    // Extract outer .deb (using 'ar')
    std::string cmd_ar = "cd " + temp_dir.string() + " && ar x " + full_deb_path.string() + " > /dev/null 2>&1";
    if (std::system(cmd_ar.c_str()) != 0) {
        spdlog::error("[SLF] Failed to extract .deb archive.");
        fs::remove_all(temp_dir);
        return;
    }

    // Find and Extract data.tar.*
    fs::path data_tar;
    bool found_tar = false;
    for (const auto& entry : fs::directory_iterator(temp_dir)) {
        std::string name = entry.path().filename().string();
        if (name.find("data.tar") != std::string::npos) {
            data_tar = entry.path();
            found_tar = true;
            break;
        }
    }

    if (!found_tar) {
        spdlog::error("[SLF] data.tar not found inside .deb");
        fs::remove_all(temp_dir);
        return;
    }

    std::string cmd_tar = "tar -xf " + data_tar.string() + " -C " + temp_dir.string() + " > /dev/null 2>&1";
    if (std::system(cmd_tar.c_str()) != 0) {
        spdlog::error("[SLF] Failed to unpack data.tar");
        fs::remove_all(temp_dir);
        return;
    }

    std::vector<std::string> found_binaries;
    std::set<std::string> found_lib_dirs;

    spdlog::info("[SLF] Scanning for assets...");

    for (const auto& entry : fs::recursive_directory_iterator(temp_dir)) {
        if (!entry.is_regular_file()) continue;

        fs::path current_path = entry.path();
        std::string filename = current_path.filename().string();
        
        // Get path relative to the extraction root (e.g., usr/bin/app)
        fs::path relative_p = fs::relative(current_path, temp_dir);
        std::string rel_str = relative_p.string();

        // Check for Shared Objects (.so)
        if (filename.find(".so") != std::string::npos) {
            // We save the DIRECTORY containing the lib
            found_lib_dirs.insert(relative_p.parent_path().string());
        }

        // Check for Binaries
        // Criteria: Executable AND (in bin/sbin OR no extension) AND not a library
        if (is_executable(current_path) && filename.find(".so") == std::string::npos) {
            if (rel_str.find("/bin") != std::string::npos || rel_str.find("/sbin") != std::string::npos) {
                found_binaries.push_back(rel_str);
            }
        }
    }

    // Generate instructions.json
    json instructions;
    instructions["package_file"] = deb_filename;
    instructions["binaries"] = found_binaries;
    instructions["exported_library_paths"] = std::vector<std::string>(found_lib_dirs.begin(), found_lib_dirs.end());

    fs::path json_out = store_path / "instructions.json";
    std::ofstream o(json_out);
    o << std::setw(4) << instructions << std::endl;

    spdlog::info("[SLF] Generated {}", json_out.string());

    // Cleanup
    fs::remove_all(temp_dir);
    spdlog::info("[SLF] Cleanup complete.");
}
