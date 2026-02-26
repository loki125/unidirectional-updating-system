#include "Algorithms.hpp"

#include <queue>
#include <stack>
#include <functional>
#include <spdlog/spdlog.h> 

GSO::GSO(const std::vector<json>& packages) : pgraph(graph_builder(packages)){  

    Graph& graph = pgraph.graph();

    //Detect and Break Cycles
    resolve_circular_dependencies(graph);
    std::vector<std::size_t> sorted_vector = sort_algo(graph);

    this->sorted_pkgs = phrase_sorted_vector(pgraph, sorted_vector);
}

std::vector<json> GSO::subgraph_order(const std::string& name, const std::string& version){
    // extract subgraph from the point of pkg
    // then see all packages it that sub graph and make a new sorted vector that consists only of the packages in the subgraph
    if(sorted_pkgs.empty())
        throw std::runtime_error("sorted vector not initialized");

    json main_pkg;
    Package pkg(name, version);

    std::optional<std::size_t> pkg_place = this->pgraph.get_id(pkg);
    if (!pkg_place.has_value()) 
        throw std::runtime_error("Package not found in GSO");
    

    std::unordered_set<std::size_t> descendants = get_descendants(this->pgraph.graph(), pkg_place.value());
    std::vector<json> subgraph_vector;

    for (const auto& pkg_json : this->sorted_pkgs) {
        Package p(pkg_json);
        auto id = pgraph.get_id(p);
        if (id && descendants.find(*id) != descendants.end())
            subgraph_vector.push_back(pkg_json);
    }

    return subgraph_vector;
}

PackageGraph GSO::graph_builder(const std::vector<json>& pkg_list)
{
    if (pkg_list.empty())
        throw std::runtime_error("Failed to extract packages");

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

std::vector<std::size_t> GSO::sort_algo(const Graph &graph){
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
    std::vector<std::size_t> out_degree_g(n, 0); // Represents dependency count

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
    std::vector<std::size_t> result; // The list

    //insert all leafs (0 out-degree) into the queue
    for (std::size_t i = 0; i < n; ++i) {
        if (out_degree_g[i] == 0) {
            q.push(i);
            result.push_back(i);
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
                result.push_back(neighbor);
                
                // Add to queue to "go up to the next level"
                q.push(neighbor);
            }
            
        }
    }

    if (result.size() != n) 
        throw std::runtime_error("Circular dependency detected"); // circular dependency detected

    return result;
}

std::unordered_set<std::size_t> GSO::get_descendants(const Graph &g, std::size_t start)
{
    std::unordered_set<std::size_t> result;
    std::vector<bool> visited(g.size(), false);
    std::stack<std::size_t> st;

    st.push(start);
    visited[start] = true;

    while (!st.empty()) {
        auto node = st.top();
        st.pop();

        for (auto n : g.neighbors(node)) {
            if (!visited[n]) {
                visited[n] = true;
                result.insert(n);   // collect descendant
                st.push(n);
            }
        }
    }

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
    std::size_t timer = 0;

    // store edges to remove 
    std::vector<std::pair<std::size_t, std::size_t>> edges_to_cut;

    // DFS Lambda
    auto SCC_DFS = [&](auto&& self, std::size_t u) -> void {
        disc[u] = low[u] = ++timer;
        st.push(u);
        stackMember[u] = true;

        for (std::size_t v : graph.neighbors(u)) {
            if (disc[v] == -1) {
                self(self, v);                     // ← recursion HERE
                low[u] = std::min(low[u], low[v]);
            } else if (stackMember[v]) {
                low[u] = std::min(low[u], disc[v]);
            }
        }

        // root of SCC
        if (low[u] == disc[u]) {
            std::vector<std::size_t> component;
            while (true) {
                std::size_t v = st.top();
                st.pop();
                stackMember[v] = false;
                component.push_back(v);
                if (u == v) break;
            }
            if(component.size() == 1)
                spdlog::warn("[GSO] self pointing SCC detected. invalid ID: {}", component[0]);

            else if (component.size() == 2) {
                std::size_t nodeA = component[0], nodeB = component[1]; 
                
                spdlog::warn("[Cycle Breaker] Size 2 Cycle Detected: ID {} <--> ID {}", nodeA, nodeB); 
                edges_to_cut.push_back({nodeA, nodeB});
            }
            else if (component.size() > 2) {
                throw std::runtime_error("[GSO] Complex Cycle Detected");
            }
        }
    };

    // handles disconnected graphs
    for (std::size_t i = 0; i < n; i++) {
        if (disc[i] == -1) {
            SCC_DFS(SCC_DFS, i);
        }
    }

    // excecute edge removals
    for (const auto& edge : edges_to_cut) {
        graph.rm_edge(edge.first, edge.second);
    }

}

std::vector<json> GSO::phrase_sorted_vector(const PackageGraph &pgraph, const std::vector<std::size_t> &sorted_vector)
{
    std::vector<json> sorted_packages;
    for (const auto& index : sorted_vector) {
        json pkg_json = pgraph.get_package(index).package_json;
        
        sorted_packages.push_back(pkg_json);
        spdlog::info("\n[GSO] package index {}\nSorted Package: {}", index, pkg_json["Filename"].get<std::string>());
    }
    return sorted_packages;
}

/*

this is just so i know where the implementation of GSO ends and RecipeMaker starts

*/

RecipeMaker::RecipeMaker(const json& manifest) : 

packages(manifest["Packages"].get<std::vector<json>>()), 
global_sort(GSO(this->packages))
{}

void RecipeMaker::generate_recipe(const fs::path& directory_path, PackageReader& reader, const json& forest) {

    fs::path pkg_path = reader.get_pkg_path(directory_path);

    // Extract Info using the Reader
    std::string pkg_name = reader.get_name(pkg_path.string());
    std::string pkg_version = reader.get_version(pkg_path.string());
    json recipe;
    
    recipe["package_name"] = pkg_name;
    recipe["version"] = pkg_version;

    // Calculate Recursive Mounts
    json mount_instr = calculate_mounts(pkg_name, pkg_version);
    recipe["mount_instructions"] = mount_instr["required_mounts"].empty() ? json::array() : mount_instr;

    // Get Scripts
    recipe.update(forest);
    recipe["scripts"] = reader.get_scripts(pkg_path.string());

    // Write Output
    fs::path recipe_out = directory_path / "recipe.json";
    std::ofstream out(recipe_out);
    out << recipe.dump(4);
    out.close();

    spdlog::info("Recipe generated successfully at {}", recipe_out.string());
}


json RecipeMaker::calculate_mounts(const std::string& pkg_name, const std::string& pkg_version) {
    
    json required_mounts = json::array();
    for(const auto& pkg : this->global_sort.subgraph_order(pkg_name, pkg_version))
        required_mounts.push_back(pkg["Store_Path"]);
        
    json instr;
    instr["required_mounts"] = required_mounts;
    return instr;
}
