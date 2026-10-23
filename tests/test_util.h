// Helpers shared by the unit tests.
#pragma once

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "assertion.h"
#include "graph.h"
#include "verilog_parser.h"

namespace sva::test {

inline std::string read_file(const std::string& path) {
    std::ifstream in(path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// A parsed benchmark with both machines built.
struct Bench {
    FsmModule fsm;
    Graph mealy;
    Graph moore;
    std::vector<Assertion> assertions;
};

// Builds a Bench from FSM source; assertions come from `sva` or, if empty,
// from the FSM source itself.
inline Bench make_bench(const std::string& fsm_text, const std::string& sva = "") {
    Bench b;
    b.fsm = parse_fsm(fsm_text);
    b.assertions = parse_assertions(sva.empty() ? fsm_text : sva, SignalTable(b.fsm));
    b.mealy = build_mealy(b.fsm);
    b.moore = mealy2moore(b.mealy);
    return b;
}

inline Bench load_bench(const std::string& name) {
    const std::string dir = std::string(SVA_GEN_SOURCE_DIR) + "/benchmarks/" + name + "/";
    const std::string sva = read_file(dir + "assertions.sva");
    return make_bench(read_file(dir + "fsm.v"), sva);
}

inline const std::vector<std::string>& bench_names() {
    static const std::vector<std::string> names = {"contest_example", "handshake", "traffic_light",
                                                   "rand_m", "rand_l"};
    return names;
}

inline const Node* find_node(const Graph& g, const std::string& name) {
    for (const auto& n : g.node_list()) {
        if (n->name == name) return n.get();
    }
    return nullptr;
}

}  // namespace sva::test
