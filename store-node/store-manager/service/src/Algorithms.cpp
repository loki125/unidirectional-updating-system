#include "Algorithms.hpp"

#include <queue>
#include <stack>
#include <functional>
#include <spdlog/spdlog.h> 

std::vector<json> GSO::sort(const json &manifest_json)
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

std::unordered_map<std::string, int> GSO::get_global_priority_map(const std::vector<json>& sorted_pkgs) {
    std::unordered_map<std::string, int> map;
    for (size_t i = 0; i < sorted_pkgs.size(); ++i) {
        std::string name = sorted_pkgs[i]["Package"];
        map[name] = static_cast<int>(i);
    }
    return map;
}

PackageGraph GSO::graph_builder(const json &manifest_json)
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

std::vector<int> GSO::sort_algo(const Graph &graph){
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

    return result;
}

void GSO::resolve_circular_dependencies(Graph &graph){
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
                throw std::runtime_error("[GSO] [Cycle Breaker] Complex Cycle Detected");
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
        graph.rm_edge(edge.first, edge.second);
    }

}

std::vector<json> GSO::phrase_sorted_vector(const PackageGraph &pgraph, const std::vector<int> &sorted_vector)
{
    std::vector<json> sorted_packages;
    for (const auto& index : sorted_vector) {
        json pkg_json = pgraph.get_package(index).package_json;
        
        sorted_packages.push_back(pkg_json);
        spdlog::info("\n[GSO] package index {}\nSorted Package: {}\n", index, pkg_json.dump(4));
    }
    return sorted_packages;
}

/*

this is just so i know where the implementation of GSO ends and RecipeMaker starts

*/

RecipeMaker::RecipeMaker(const json& manifest) : global_manifest(manifest) {
    if (!manifest.contains("Packages")) 
        throw std::runtime_error("Invalid Manifest");

    // Calculate Global Sort Order (GSO)
    std::vector<json> sorted_pkgs = GSO::sort(manifest);
    priority_map = GSO::get_global_priority_map(sorted_pkgs);

    // Build Lookup Table
    for (const auto& pkg : manifest["Packages"]) {
        std::string name = pkg["Package"];
        pkg_lookup[name] = pkg;
    }
}

void RecipeMaker::generate_recipe(const fs::path& directory_path, const std::string& type) {

    auto reader = PackageReader::create(type);
    fs::path pkg_path = reader->get_pkg_path(directory_path);

    // Extract Info using the Reader
    std::string pkg_name = reader->get_name(pkg_path.string());
    
    // Verify existence in global manifest
    if (pkg_lookup.find(pkg_name) == pkg_lookup.end()) {
        throw std::runtime_error("Package '" + pkg_name + "' not found in Global Manifest");
    }

    json my_meta = pkg_lookup[pkg_name];
    json recipe;
    
    recipe["package_name"] = pkg_name;
    recipe["version"] = reader->get_version(pkg_path.string());

    // Flatten Dependencies
    std::vector<std::string> flat_deps;
    if (my_meta.contains("Dependencies")) {
        for (const auto& group : my_meta["Dependencies"]) {
            if (!group.empty()) {
                std::string raw = group[0];
                std::string clean = raw.substr(0, raw.find(' '));
                flat_deps.push_back(clean);
            }
        }
    }
    recipe["dependencies"] = flat_deps;

    // Calculate Recursive Mounts
    json mount_instr = calculate_mounts(pkg_name, flat_deps);
    recipe["mount_instructions"] = mount_instr;

    // Get Files & Scripts
    recipe["symlink_forest"] = reader->get_files(pkg_path.string());
    recipe["scripts"] = reader->get_scripts(pkg_path.string());

    // Write Output
    fs::path recipe_out = directory_path / "recipe.json";
    std::ofstream out(recipe_out);
    out << recipe.dump(4);
    out.close();

    spdlog::info("Recipe generated: {}", recipe_out.string());
}


json RecipeMaker::calculate_mounts(const std::string& my_name, const std::vector<std::string>& direct_deps) {
    std::set<std::string> recursive_deps;
    std::vector<std::string> q = direct_deps;
    size_t head = 0;

    // BFS
    while(head < q.size()){
        std::string curr = q[head++];
        if (recursive_deps.count(curr)) continue;
        recursive_deps.insert(curr);

        if (pkg_lookup.count(curr)) {
            for(const auto& g : pkg_lookup[curr]["Dependencies"]){
                if(!g.empty()) {
                    std::string raw = g[0];
                    std::string clean = raw.substr(0, raw.find(' '));
                    q.push_back(clean);
                }
            }
        }
    }

    std::vector<std::string> sorted_mounts(recursive_deps.begin(), recursive_deps.end());
    
    // Sort by Priority
    std::sort(sorted_mounts.begin(), sorted_mounts.end(), 
        [&](const std::string& a, const std::string& b) {
            return priority_map[a] < priority_map[b];
        }
    );

    json required_mounts = json::array();
    for(const auto& pkg : sorted_mounts) {
        if (pkg_lookup.count(pkg) && pkg_lookup[pkg].contains("Store_Path")) {
            required_mounts.push_back(pkg_lookup[pkg]["Store_Path"]);
        }
    }

    json instr;
    instr["lowerdir_priority"] = priority_map[my_name];
    instr["required_mounts"] = required_mounts;
    return instr;
}
