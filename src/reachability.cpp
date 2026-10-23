#include "reachability.h"

#include <algorithm>

namespace sva {

std::string reset_line(int input_width) { return "1" + std::string(input_width, '0'); }
std::string idle_line(int input_width) { return "0" + std::string(input_width, '0'); }

namespace {

// One clock cycle of the search: leave state `from` along `edge`, with the
// tracked input bits set to `q` (bit i of q is tracked input bit i).
struct Step {
    int from = -1;
    const Edge* edge = nullptr;
    unsigned q = 0;
};

class SequenceSearch {
public:
    SequenceSearch(const Graph& moore, const Assertion& assertion) : g_(moore), a_(assertion) {
        for (const Condition* c : {&a_.antecedent, &a_.consequent}) {
            if (c->bit.is_input &&
                std::find(tracked_.begin(), tracked_.end(), c->bit.pos) == tracked_.end()) {
                tracked_.push_back(c->bit.pos);
            }
        }
        combos_ = 1U << tracked_.size();
        num_states_ = static_cast<int>(g_.node_list().size() * combos_);
    }

    SequenceResult run() {
        // 1. All search states reachable from reset, with BFS distance and a
        //    parent step for the shortest path back (the report's A_i).
        //    The reset cycle drives every input bit to 0.
        const int start = state(g_.initial, 0);
        std::vector<int> dist(num_states_, -1);
        std::vector<Step> parent(num_states_);
        std::vector<int> order{start};
        dist[start] = 0;
        for (size_t i = 0; i < order.size(); ++i) {
            const int s = order[i];
            for_each_step(s, [&](const Step& step, int next) {
                if (dist[next] >= 0) return;
                dist[next] = dist[s] + 1;
                parent[next] = step;
                order.push_back(next);
            });
        }

        // 2. Layer 0 (R_0): states entered by a step on which the antecedent
        //    fires. Scanning in BFS order makes the first trigger the closest.
        // 3. Layer j (R_j): successors of layer j-1. The first a-1 steps are
        //    free; from step a to b the consequent must NOT hold.
        const int b = a_.b;
        std::vector<std::vector<int>> layer(b + 1);
        std::vector<std::vector<Step>> via(b + 1, std::vector<Step>(num_states_));
        auto extend = [&](int j, int s) {
            for_each_step(s, [&](const Step& step, int next) {
                const bool in_window = j >= a_.a;
                if (j == 0 && !holds(a_.antecedent, step)) return;
                if (in_window && holds(a_.consequent, step)) return;
                if (via[j][next].edge) return;  // already in this layer
                via[j][next] = step;
                layer[j].push_back(next);
            });
        };
        for (int s : order) extend(0, s);
        for (int j = 1; j <= b && !layer[j - 1].empty(); ++j) {
            for (int s : layer[j - 1]) extend(j, s);
        }
        if (layer[b].empty()) return {};

        // 4. Back-trace from the first target in R_b through the layers to the
        //    trigger, then along BFS parents (a shortest path) to reset.
        std::vector<Step> steps;
        int s = layer[b].front();
        for (int j = b; j >= 0; --j) {
            steps.push_back(via[j][s]);
            s = via[j][s].from;
        }
        SequenceResult result;
        result.found = true;
        result.trigger_cycle = dist[s] + 1;
        while (s != start) {
            steps.push_back(parent[s]);
            s = parent[s].from;
        }
        std::reverse(steps.begin(), steps.end());

        result.lines.push_back(reset_line(g_.input_width));
        for (const Step& step : steps) result.lines.push_back(input_line(step));
        return result;
    }

private:
    int state(const Node* n, unsigned q) const { return n->id * static_cast<int>(combos_) + q; }
    const Node* node(int s) const { return g_.node_list()[s / combos_].get(); }
    unsigned tracked_values(int s) const { return s % combos_; }

    // A cube of the edge that agrees with q on every tracked bit, or nullptr.
    const std::string* cube_for(const Edge& e, unsigned q) const {
        for (const std::string& cube : e.input) {
            bool ok = true;
            for (size_t i = 0; i < tracked_.size() && ok; ++i) {
                const char want = ((q >> i) & 1U) ? '1' : '0';
                ok = cube[tracked_[i]] == 'x' || cube[tracked_[i]] == want;
            }
            if (ok) return &cube;
        }
        return nullptr;
    }

    template <typename Visit>
    void for_each_step(int s, Visit&& visit) const {
        for (const Edge* e : node(s)->out_edges) {
            for (unsigned q = 0; q < combos_; ++q) {
                if (cube_for(*e, q)) visit(Step{s, e, q}, state(e->tail, q));
            }
        }
    }

    // Output bits are read from the Moore edge (head/tail outputs); tracked
    // input bits from the previous and the chosen input values.
    bool holds(const Condition& c, const Step& step) const {
        if (!c.bit.is_input) {
            return c.holds_on(step.edge->property[c.bit.pos], step.edge->tail->output[c.bit.pos]);
        }
        const size_t i = std::find(tracked_.begin(), tracked_.end(), c.bit.pos) - tracked_.begin();
        const char prev = ((tracked_values(step.from) >> i) & 1U) ? '1' : '0';
        const char cur = ((step.q >> i) & 1U) ? '1' : '0';
        return c.holds(prev, cur);
    }

    // Concrete input line for a step: tracked bits from q, other don't-cares 0.
    std::string input_line(const Step& step) const {
        std::string bits = *cube_for(*step.edge, step.q);
        for (size_t i = 0; i < tracked_.size(); ++i) bits[tracked_[i]] = ((step.q >> i) & 1U) ? '1' : '0';
        std::replace(bits.begin(), bits.end(), 'x', '0');
        return "0" + bits;
    }

    const Graph& g_;
    const Assertion& a_;
    std::vector<int> tracked_;  // input bit positions the assertion reads
    unsigned combos_ = 1;
    int num_states_ = 0;
};

}  // namespace

SequenceResult generate_one_sequence(const Graph& moore, const Assertion& assertion) {
    return SequenceSearch(moore, assertion).run();
}

}  // namespace sva
