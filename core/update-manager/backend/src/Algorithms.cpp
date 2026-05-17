#include "Algorithms.hpp"

#include <queue>
#include <stack>
#include <functional>
#include <spdlog/spdlog.h> 

GSO::GSO(const std::vector<PackageMetadata>& packages) : pgraph(graph_builder(packages)){  

    Graph& graph = pgraph.graph();

    std::vector<EdgeToCut> edges_to_cut = scc_detection(graph);
    this->resolve_scc(edges_to_cut);

    std::vector<std::size_t> sorted_vector = sort_algo(graph);

    this->sorted_pkgs = phrase_sorted_vector(pgraph, sorted_vector);
}

const std::vector<PackageMetadata> GSO::subgraph_order(const std::string& name, const std::string& version) const {
    if(sorted_pkgs.empty())
        throw std::runtime_error("sorted vector not initialized");

    PackageMetadata main_pkg;
    Package pkg(name, version);

    std::optional<std::size_t> pkg_place = this->pgraph.get_id(pkg);
    if (!pkg_place.has_value()) 
        throw std::runtime_error("Package not found in GSO");
    

    std::unordered_set<std::size_t> descendants = get_descendants(this->pgraph.graph(), pkg_place.value());
    std::vector<PackageMetadata> subgraph_vector;

    for (const auto& pkg_meta : this->sorted_pkgs) {
        Package p(pkg_meta);
        auto id = pgraph.get_id(p);
        if (id && descendants.find(*id) != descendants.end())
            subgraph_vector.push_back(pkg_meta);
    }

    return subgraph_vector;
}

PackageGraph GSO::graph_builder(const std::vector<PackageMetadata>& pkg_list)
{
    if (pkg_list.empty())
        throw std::runtime_error("Failed to extract packages");

    PackageGraph pgraph(pkg_list.size());

    for(const auto& pkg_meta : pkg_list){
        struct Package pkg(pkg_meta);

        std::vector<Package>& depends = pkg.dependencies;
        const auto& unprased_depends = pkg_meta.Dependencies;

        for(const auto& dep_vector : unprased_depends){
            struct Package dep{};

            dep.name = dep_vector.at(0); //package
            dep.version = dep_vector.at(1); //version

            depends.emplace_back(dep);
        }
        pgraph.add_depend(pkg);
    }

    return pgraph;
}

std::vector<std::size_t> GSO::sort_algo(const Graph &graph) {
    /*
    Kahn's Algorithm for Topological Sorting:
    Create list where in_degree[u] = count(in_degrees)
    Find all leaf nodes and add them to a queue.
    Process (BFS):
        While the queue is not empty:
            Take a node u out of the queue.
            Add it to result list.
            For every node v that depends on u:
                in_degree[v]--
                If in_degree[v] == 0:
                    add v to the queue.
    Check for Cycles: If result.size() < total number of nodes, a cycle exists.

    Reverse: Reverse the list to get the Highest Dependency order.
    */
    std::size_t n = graph.size();
    std::vector<std::size_t> in_degree(n, 0);
    std::vector<std::size_t> result;
    std::queue<std::size_t> q;

    for (std::size_t u = 0; u < n; ++u) {
        for (std::size_t v : graph.neighbors(u)) {
            in_degree[v]++;
        }
    }

    for (std::size_t i = 0; i < n; ++i) {
        if (in_degree[i] == 0) {
            q.push(i);
        }
    }

    while (!q.empty()) {
        std::size_t curr = q.front();
        q.pop();
        result.push_back(curr);

        for (std::size_t neighbor : graph.neighbors(curr)) {
            in_degree[neighbor]--;
            if (in_degree[neighbor] == 0) {
                q.push(neighbor);
            }
        }
    }

    if (result.size() != n) {
        throw std::runtime_error("Circular dependency detected");
    }

    // Reverse to get Highest to Lowest dependency order
    std::reverse(result.begin(), result.end());

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
                result.insert(n);   
                st.push(n);
            }
        }
    }

    return result;
}

void GSO::scc_dfs(std::size_t u, const Graph &graph, SCCState &state) {
    state.disc[u] = state.low[u] = ++state.timer;
    state.st.push(u);
    state.stackMember[u] = true;

    for (std::size_t v : graph.neighbors(u)) {
        if (state.disc[v] == -1) {
            scc_dfs(v, graph, state);
            state.low[u] = std::min(state.low[u], state.low[v]);
            continue;
        }

        if (state.stackMember[v]) {
            state.low[u] = std::min(state.low[u], state.disc[v]);
        }
    }

    if (state.low[u] != state.disc[u]) {
        return;
    }

    std::vector<std::size_t> component;

    while (true) {
        std::size_t v = state.st.top();
        state.st.pop();

        state.stackMember[v] = false;
        component.push_back(v);

        if (u == v) {
            break;
        }
    }

    const std::size_t size = component.size();

    if (size == 1) {
        spdlog::debug(
            "[GSO] self pointing SCC detected. invalid ID: {}",
            component[0]
        );
        return;
    }

    if (size == CONFLICT_TYPE_THRESHOLD) {
        state.edges_to_cut.push_back({
            component[0],
            component[1],
            ConflictType::SOFT
        });
        return;
    }

    auto find_cut_edge = [&](const std::vector<std::size_t> &nodes)
        -> std::pair<std::size_t, std::size_t>
    {
        auto is_in_component = [&](std::size_t node) {
            return std::find(nodes.begin(), nodes.end(), node) != nodes.end();
        };

        for (std::size_t nodeA : nodes) {
            for (std::size_t nodeB : graph.neighbors(nodeA)) {
                if (is_in_component(nodeB)) {
                    return {nodeA, nodeB};
                }
            }
        }

        return {nodes[0], nodes[1]};
    };

    auto [cut_u, cut_v] = find_cut_edge(component);

    state.edges_to_cut.push_back({
        cut_u,
        cut_v,
        ConflictType::HARD
    });
}

std::vector<EdgeToCut> GSO::scc_detection(const Graph &graph) {
    SCCState state(graph.size());

    for (std::size_t i = 0; i < graph.size(); i++) {
        if (state.disc[i] == -1) {
            scc_dfs(i, graph, state);
        }
    }

    return std::move(state.edges_to_cut);
}

void GSO::resolve_scc(const std::vector<EdgeToCut>& edges_to_cut) {
    for (const auto& edge : edges_to_cut) {
        if (edge.type == ConflictType::SOFT) {
            spdlog::warn("[Cycle Breaker] Size 2 Cycle Detected: ID {} <--> ID {}. Resolving Soft Conflict.", edge.u, edge.v);
            pgraph.graph().rm_edge(edge.u, edge.v);
            std::string dest = pgraph.get_package(edge.v).package_metadata.Package;
            PackageMetadata& source = pgraph.get_package(edge.u).package_metadata;

            std::vector<std::vector<std::string>>&  source_depends = source.Dependencies;
            size_t i = 0;
            for(std::vector<std::string>& dep : source_depends){
                if(dep[0] == dest){
                    break;
                }
                i++;
            }
            source_depends.erase(source_depends.begin() + i);
        } 
        else if (edge.type == ConflictType::HARD) {
            PackageMetadata culprit = pgraph.get_package(edge.u).package_metadata;
            
            spdlog::error("[Cycle Breaker] Hard Cycle (>2) Detected originating from {} v{}. Backtracking...", 
                          culprit.Package, culprit.Version);
            
            throw HardConflictException(culprit.Package, culprit.Version);
        }
    }
}

std::vector<PackageMetadata> GSO::phrase_sorted_vector(const PackageGraph &pgraph, const std::vector<std::size_t> &sorted_vector)
{
    std::vector<PackageMetadata> sorted_packages;
    for (const auto& index : sorted_vector) {
        const PackageMetadata pkg_meta = pgraph.get_package(index).package_metadata;
        
        sorted_packages.push_back(pkg_meta);
        spdlog::info("\n[GSO] package index {}\nSorted Package: {}", index, pkg_meta.Filename);
    }
    return sorted_packages;
}

