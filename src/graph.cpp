#include "graph.h"

#include <algorithm>
#include <deque>
#include <map>
#include <stdexcept>

namespace sva {

bool Edge::accepts(const std::string& input_bits) const {
    for (const std::string& cube : input) {
        bool match = true;
        for (size_t i = 0; i < cube.size() && match; ++i) {
            match = cube[i] == 'x' || cube[i] == input_bits[i];
        }
        if (match) return true;
    }
    return false;
}

Node* Graph::add_node(const std::string& name) {
    auto node = std::make_unique<Node>();
    node->id = static_cast<int>(nodes_.size());
    node->name = name;
    nodes_.push_back(std::move(node));
    return nodes_.back().get();
}

Edge* Graph::add_edge(Node* head, Node* tail, std::vector<std::string> input, std::string output) {
    auto edge = std::make_unique<Edge>();
    edge->head = head;
    edge->tail = tail;
    edge->input = std::move(input);
    edge->output = std::move(output);
    head->out_edges.push_back(edge.get());
    tail->in_edges.push_back(edge.get());
    edges_.push_back(std::move(edge));
    return edges_.back().get();
}

int Graph::compute_reachable_dist() {
    for (auto& n : nodes_) n->reachable_dist = Node::kUnreachable;
    std::deque<Node*> queue{initial};
    initial->reachable_dist = 0;
    int count = 0;
    while (!queue.empty()) {
        Node* n = queue.front();
        queue.pop_front();
        ++count;
        for (Edge* e : n->out_edges) {
            if (e->tail->reachable_dist == Node::kUnreachable) {
                e->tail->reachable_dist = n->reachable_dist + 1;
                queue.push_back(e->tail);
            }
        }
    }
    return count;
}

std::vector<bool> Graph::verify_assertion_failure(const std::vector<Assertion>& assertions,
                                                  const std::vector<std::string>& input) const {
    // Cycle-by-cycle values. An empty output string means "unknown" (before
    // the first reset the state register is X).
    const size_t len = input.size();
    std::vector<std::string> in_bits(len);
    std::vector<std::string> out_bits(len);
    const Node* state = nullptr;
    for (size_t t = 0; t < len; ++t) {
        const std::string& line = input[t];
        if (line.size() != static_cast<size_t>(input_width) + 1) {
            throw std::invalid_argument("input line " + std::to_string(t + 1) + " has wrong width");
        }
        in_bits[t] = line.substr(1);
        const bool reset = line[0] == '1';
        if (reset) state = initial;  // asynchronous reset holds the start state
        if (!state) continue;
        auto it = std::find_if(state->out_edges.begin(), state->out_edges.end(),
                               [&](const Edge* e) { return e->accepts(in_bits[t]); });
        if (it == state->out_edges.end()) throw std::logic_error("incomplete transition table");
        out_bits[t] = (*it)->output;
        if (!reset) state = (*it)->tail;
    }

    auto value = [&](size_t t, const SignalBit& bit) -> char {
        if (bit.is_input) return in_bits[t][bit.pos];
        return out_bits[t].empty() ? '?' : out_bits[t][bit.pos];
    };
    auto prev_value = [&](size_t t, const SignalBit& bit) { return t == 0 ? '?' : value(t - 1, bit); };

    std::vector<bool> failed(assertions.size(), false);
    for (size_t i = 0; i < assertions.size(); ++i) {
        const Assertion& as = assertions[i];
        const SignalBit& cbit = as.consequent.bit;
        for (size_t trigger = 0; trigger + as.b < len && !failed[i]; ++trigger) {
            if (!as.antecedent.holds(prev_value(trigger, as.antecedent.bit),
                                     value(trigger, as.antecedent.bit))) {
                continue;
            }
            bool satisfied = false;
            bool decided = true;  // every window value must be known (no X)
            for (size_t t = trigger + as.a; t <= trigger + as.b; ++t) {
                const char prev = prev_value(t, cbit);
                const char cur = value(t, cbit);
                decided = decided && cur != '?' && prev != '?';
                satisfied = satisfied || as.consequent.holds(prev, cur);
            }
            failed[i] = decided && !satisfied;
        }
    }
    return failed;
}

namespace {

// Covers a sorted set of minterms with disjoint cubes over the low
// `free_bits` bits, by Shannon expansion on the highest free bit. A full
// subspace becomes one all-x cube, and when both halves have the same cover
// the split bit becomes 'x' (so {01, 11} gives "x1", as written in casex).
std::vector<std::string> cover_minterms(const uint32_t* begin, const uint32_t* end, int free_bits) {
    const size_t size = static_cast<size_t>(end - begin);
    if (size == 0) return {};
    if (size == (size_t{1} << free_bits)) return {std::string(free_bits, 'x')};

    const uint32_t bit = 1U << (free_bits - 1);
    const uint32_t* split = std::partition_point(begin, end, [&](uint32_t m) { return !(m & bit); });
    const std::vector<std::string> low = cover_minterms(begin, split, free_bits - 1);
    const std::vector<std::string> high = cover_minterms(split, end, free_bits - 1);

    std::vector<std::string> cubes;
    if (low == high) {
        for (const std::string& c : low) cubes.push_back('x' + c);
        return cubes;
    }
    for (const std::string& c : low) cubes.push_back('0' + c);
    for (const std::string& c : high) cubes.push_back('1' + c);
    return cubes;
}

std::string state_name(const FsmModule& fsm, uint64_t value, int width) {
    std::string fallback;
    for (const auto& [name, v] : fsm.parameters) {
        if (v.bits != value) continue;
        if (v.width == width) return name;
        if (fallback.empty()) fallback = name;
    }
    return fallback.empty() ? "S" + std::to_string(value) : fallback;
}

}  // namespace

Graph build_mealy(const FsmModule& fsm) {
    Graph g;
    for (const Port& p : fsm.inputs) g.input_width += p.width;
    for (const Port& p : fsm.outputs) g.output_width += p.width;
    if (g.input_width > kMaxInputWidth) {
        throw std::runtime_error("input wider than " + std::to_string(kMaxInputWidth) + " bits");
    }

    // Evaluation environment: parameters plus every declared signal. Before
    // each evaluation all regs are cleared, so outputs that a branch does not
    // assign read as 0, and the next state starts as the present state (the
    // usual "nstate = pstate" default). The environment is reused for speed;
    // unordered_map references stay valid, so we keep pointers into it.
    Env env(fsm.parameters.begin(), fsm.parameters.end());
    for (const Port& p : fsm.inputs) env[p.name] = Value::known(0, p.width);
    for (const auto& [name, port] : fsm.regs) env[name] = Value::known(0, port.width);
    std::vector<Value*> regs;
    for (const auto& [name, port] : fsm.regs) regs.push_back(&env.at(name));
    std::vector<Value*> inputs;
    for (const Port& p : fsm.inputs) inputs.push_back(&env.at(p.name));
    std::vector<Value*> outputs;
    for (const Port& p : fsm.outputs) outputs.push_back(&env.at(p.name));
    Value& present = env.at(fsm.state_reg);
    Value& next = env.at(fsm.next_state_reg);
    const int state_width = fsm.regs.at(fsm.state_reg).width;

    std::map<uint64_t, Node*> by_value;
    std::deque<uint64_t> work;
    auto node_for = [&](uint64_t value) {
        auto it = by_value.find(value);
        if (it != by_value.end()) return it->second;
        Node* n = g.add_node(state_name(fsm, value, state_width));
        by_value[value] = n;
        work.push_back(value);
        return n;
    };
    g.initial = node_for(fsm.reset_value.bits & width_mask(state_width));

    const uint32_t num_inputs = 1U << g.input_width;
    while (!work.empty()) {
        const uint64_t state = work.front();
        work.pop_front();
        Node* head = by_value.at(state);

        // (next state, output) -> input minterms producing it
        std::map<std::pair<uint64_t, std::string>, std::vector<uint32_t>> groups;
        for (uint32_t v = 0; v < num_inputs; ++v) {
            for (Value* r : regs) r->bits = 0;
            present.bits = state;
            next.bits = state;
            int shift = g.input_width;
            for (Value* in : inputs) {
                shift -= in->width;
                in->bits = (v >> shift) & width_mask(in->width);
            }
            for (const auto& block : fsm.comb_blocks) execute(*block, env);

            std::string out;
            for (const Value* o : outputs) {
                for (int b = o->width - 1; b >= 0; --b) out += ((o->bits >> b) & 1U) ? '1' : '0';
            }
            groups[{next.bits, out}].push_back(v);
        }

        for (auto& [key, minterms] : groups) {
            g.add_edge(head, node_for(key.first),
                       cover_minterms(minterms.data(), minterms.data() + minterms.size(), g.input_width),
                       key.second);
        }
    }

    const std::string zeros(g.input_width, '0');
    for (const Edge* e : g.initial->out_edges) {
        if (e->accepts(zeros)) g.reset_output = e->output;
    }
    return g;
}

Graph mealy2moore(const Graph& mealy) {
    Graph moore;
    moore.input_width = mealy.input_width;
    moore.output_width = mealy.output_width;
    moore.reset_output = mealy.reset_output;

    // Per Mealy node: (output, Moore node) for every distinct incoming output,
    // in order of first appearance -- the report's pseudo code.
    std::vector<std::vector<std::pair<std::string, Node*>>> split(mealy.node_list().size());
    auto moore_node = [&](const Node* m, const std::string& out) {
        auto& entries = split[m->id];
        for (const auto& [o, n] : entries) {
            if (o == out) return n;
        }
        Node* n = moore.add_node(m->name + "/" + out);
        n->output = out;
        n->mealy_id = m->id;
        entries.emplace_back(out, n);
        return n;
    };

    for (const auto& m : mealy.node_list()) {
        for (const Edge* e : m->in_edges) moore_node(m.get(), e->output);
    }
    // Reset puts the machine in the start state with the reset-cycle output;
    // this node may not have an in-edge with the same output.
    moore.initial = moore_node(mealy.initial, mealy.reset_output);

    // Every Moore copy of m inherits m's out-edges; the tail is the copy of
    // the next state that carries this edge's output.
    for (const auto& m : mealy.node_list()) {
        const auto& copies = split[m->id];
        for (const auto& [out, n] : copies) {
            for (const Edge* e : m->out_edges) {
                Edge* me = moore.add_edge(n, moore_node(e->tail, e->output), e->input, "");
                me->property.resize(out.size());
                for (size_t b = 0; b < out.size(); ++b) {
                    me->property[b] = out[b] == e->output[b] ? BitChange::Stable
                                      : e->output[b] == '1'  ? BitChange::Rise
                                                             : BitChange::Fall;
                }
            }
        }
    }
    return moore;
}

}  // namespace sva
