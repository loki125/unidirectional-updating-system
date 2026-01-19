
#include <cstdint>
#include <vector>


class Graph {
public:
    explicit Graph(std::size_t node_count);

    void add_edge(std::size_t from, std::size_t to);

    const std::vector<std::size_t>& neighbors(std::size_t node) const;

    std::size_t size() const noexcept;

private:
    std::vector<std::vector<std::size_t>> adj_;
};