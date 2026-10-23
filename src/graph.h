// Node / Edge / Graph classes from the report (Sec. III-A).
//
// The same classes hold both machines:
//   * the Mealy machine built from fsm.v: edges carry an input condition and
//     an output string, nodes have no output;
//   * the Moore machine from mealy2moore(): each node carries the output
//     produced when entering it, and each edge records per output bit whether
//     it rises, falls or stays (Edge::property).
//
// Bit strings are MSB first. Input conditions are lists of cubes over the
// input bits using '0', '1' and 'x' (don't care).
#pragma once

#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "assertion.h"
#include "verilog_parser.h"

namespace sva {

struct Edge;

struct Node {
    static constexpr unsigned kUnreachable = std::numeric_limits<unsigned>::max();

    int id = 0;
    std::string name;
    std::string output;  // Moore only
    int mealy_id = -1;   // Moore only: the Mealy state this node was split from
    std::vector<Edge*> out_edges;
    std::vector<Edge*> in_edges;
    unsigned reachable_dist = kUnreachable;  // BFS distance from the initial node
};

struct Edge {
    Node* head = nullptr;  // present state
    Node* tail = nullptr;  // next state
    std::vector<std::string> input;  // cubes; casex priority can need more than one
    std::string output;              // Mealy only
    std::vector<BitChange> property; // Moore only, one entry per output bit

    bool accepts(const std::string& input_bits) const;
};

class Graph {
public:
    Graph() = default;
    Graph(Graph&&) = default;
    Graph& operator=(Graph&&) = default;

    Node* add_node(const std::string& name);
    Edge* add_edge(Node* head, Node* tail, std::vector<std::string> input, std::string output);

    const std::vector<std::unique_ptr<Node>>& node_list() const { return nodes_; }
    const std::vector<std::unique_ptr<Edge>>& edge_list() const { return edges_; }

    // Fills Node::reachable_dist by BFS from `initial`; returns #reachable nodes.
    int compute_reachable_dist();

    // Simulates the Mealy machine on `input` (one "rst in[msb..lsb]" string per
    // cycle) and returns, per assertion, whether it fails at least once.
    std::vector<bool> verify_assertion_failure(const std::vector<Assertion>& assertions,
                                               const std::vector<std::string>& input) const;

    Node* initial = nullptr;
    int input_width = 0;
    int output_width = 0;
    // Output during a reset cycle: the start state's output for all-zero input.
    std::string reset_output;

private:
    std::vector<std::unique_ptr<Node>> nodes_;
    std::vector<std::unique_ptr<Edge>> edges_;
};

// Largest input vector we enumerate per state when building the Mealy graph.
constexpr int kMaxInputWidth = 16;

// Builds the Mealy graph of the states reachable from reset.
Graph build_mealy(const FsmModule& fsm);

// Splits each Mealy state by the distinct outputs on its incoming edges, so
// that rise/fall of an output bit can be read off a single edge.
Graph mealy2moore(const Graph& mealy);

}  // namespace sva
