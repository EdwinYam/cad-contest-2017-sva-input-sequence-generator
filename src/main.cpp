// sva_gen: generate an input sequence that fails as many SVA rules as possible.
//
//   sva_gen <fsm.v> [assertions.sva] -o <sequence.txt> [--method A|B|C] [--plan]
//   sva_gen -i fsm.v -o input_sequence          (contest form)
//
// Without a separate .sva file the assertions are read from fsm.v itself, as
// in the contest benchmarks.
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "assertion.h"
#include "graph.h"
#include "left_edge.h"
#include "methods.h"
#include "reachability.h"
#include "verilog_parser.h"

namespace {

using Clock = std::chrono::steady_clock;

struct Options {
    std::string fsm_path;
    std::string sva_path;
    std::string out_path;
    sva::Method method = sva::Method::C;
    bool plan = false;
};

void usage() {
    std::cerr << "usage: sva_gen <fsm.v> [assertions.sva] -o <sequence.txt> [--method A|B|C] [--plan]\n"
                 "       sva_gen -i <fsm.v> -o <sequence.txt>\n";
}

bool parse_args(int argc, char** argv, Options& opt) {
    std::vector<std::string> positional;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool has_value = i + 1 < argc;
        if ((arg == "-o" || arg == "-i" || arg == "--method") && !has_value) return false;
        if (arg == "-o") {
            opt.out_path = argv[++i];
        } else if (arg == "-i") {
            opt.fsm_path = argv[++i];
        } else if (arg == "--method") {
            const std::string m = argv[++i];
            if (m == "A") opt.method = sva::Method::A;
            else if (m == "B") opt.method = sva::Method::B;
            else if (m == "C") opt.method = sva::Method::C;
            else return false;
        } else if (arg == "--plan") {
            opt.plan = true;
        } else if (!arg.empty() && arg[0] == '-') {
            return false;
        } else {
            positional.push_back(arg);
        }
    }
    if (opt.fsm_path.empty() && !positional.empty()) {
        opt.fsm_path = positional.front();
        positional.erase(positional.begin());
    }
    if (!positional.empty()) {
        opt.sva_path = positional.front();
        positional.erase(positional.begin());
    }
    return positional.empty() && !opt.fsm_path.empty() && !opt.out_path.empty();
}

std::string read_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open " + path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

double ms_since(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

const char* method_name(sva::Method m) {
    return m == sva::Method::A ? "A" : m == sva::Method::B ? "B" : "C";
}

void print_plan(const sva::Graph& moore, const std::vector<sva::Assertion>& assertions) {
    std::vector<int> k(assertions.size());
    for (size_t i = 0; i < assertions.size(); ++i) {
        k[i] = sva::generate_one_sequence(moore, assertions[i]).trigger_cycle;
    }
    std::vector<sva::Net> nets = sva::group_into_nets(assertions, k, sva::priority_order(assertions));
    const int tracks = sva::assign_tracks_left_edge(nets);
    std::printf("plan (analysis only): %zu nets on %d tracks\n", nets.size(), tracks);
    for (const sva::Net& net : nets) {
        std::printf("  track %d  [%d, %d]  ", net.track, net.left, net.right);
        for (size_t i : net.members) std::printf(" %s", assertions[i].name.c_str());
        std::printf("\n");
    }
}

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    if (!parse_args(argc, argv, opt)) {
        usage();
        return 2;
    }
    try {
        const auto t0 = Clock::now();
        const std::string fsm_text = read_file(opt.fsm_path);
        const sva::FsmModule fsm = sva::parse_fsm(fsm_text);
        const sva::SignalTable signals(fsm);
        const std::vector<sva::Assertion> assertions =
            sva::parse_assertions(opt.sva_path.empty() ? fsm_text : read_file(opt.sva_path), signals);
        sva::Graph mealy = sva::build_mealy(fsm);
        sva::Graph moore = sva::mealy2moore(mealy);
        const int reachable = moore.compute_reachable_dist();
        const double build_ms = ms_since(t0);

        const auto t1 = Clock::now();
        const sva::GenerationResult r = sva::run_method(opt.method, mealy, moore, assertions);
        const double gen_ms = ms_since(t1);

        std::ofstream out(opt.out_path);
        if (!out) throw std::runtime_error("cannot write " + opt.out_path);
        for (const std::string& line : r.sequence) out << line << '\n';

        int failed = 0;
        for (bool f : r.failed) failed += f;
        std::printf("fsm: %zu Mealy states, %zu Mealy edges -> %zu Moore nodes (%d reachable), %zu Moore edges\n",
                    mealy.node_list().size(), mealy.edge_list().size(), moore.node_list().size(), reachable,
                    moore.edge_list().size());
        std::printf("method %s: %d/%zu assertions failed, sequence length %zu, %d pieces generated, "
                    "%d proven unfailable\n",
                    method_name(opt.method), failed, assertions.size(), r.sequence.size(),
                    r.sequences_generated, r.proven_unfailable);
        std::printf("runtime: parse+build %.3f ms, generate %.3f ms, total %.3f ms\n", build_ms, gen_ms,
                    build_ms + gen_ms);
        for (size_t i = 0; i < assertions.size(); ++i) {
            std::printf("  [%s] %-16s %s\n", r.failed[i] ? "FAIL" : "pass", assertions[i].name.c_str(),
                        assertions[i].str().c_str());
        }
        if (opt.plan) print_plan(moore, assertions);
    } catch (const std::exception& e) {
        std::cerr << "sva_gen: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
