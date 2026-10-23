#include "left_edge.h"

#include <algorithm>

namespace sva {
namespace {

// Rules 1 and 2: a $rose trigger cannot fulfil a consequent on another bit or
// a $fell of the same bit (and symmetrically for $fell).
bool trigger_cannot_fulfil(const Assertion& trig, const Assertion& other) {
    const Condition& t = trig.antecedent;
    const Condition& c = other.consequent;
    const bool same_bit = t.bit.same_bit(c.bit);
    if (t.change == Change::Rose) return !same_bit || c.change == Change::Fell;
    if (t.change == Change::Fell) return !same_bit || c.change == Change::Rose;
    return false;
}

}  // namespace

bool compatible(const Assertion& x, int kx, const Assertion& y, int ky) {
    const bool disjoint = kx > ky + y.b || ky > kx + x.b;  // rule 3
    return disjoint || (trigger_cannot_fulfil(x, y) && trigger_cannot_fulfil(y, x));
}

std::vector<Net> group_into_nets(const std::vector<Assertion>& assertions, const std::vector<int>& k,
                                 const std::vector<size_t>& order) {
    std::vector<Net> nets;
    for (size_t i : order) {
        if (k[i] < 0) continue;  // never triggered: nothing to place
        auto fits = [&](const Net& net) {
            return std::all_of(net.members.begin(), net.members.end(), [&](size_t j) {
                return compatible(assertions[i], k[i], assertions[j], k[j]);
            });
        };
        auto it = std::find_if(nets.begin(), nets.end(), fits);
        if (it == nets.end()) {
            nets.push_back(Net{{}, k[i], k[i] + assertions[i].b, 0});
            it = nets.end() - 1;
        }
        it->members.push_back(i);
        it->left = std::min(it->left, k[i]);
        it->right = std::max(it->right, k[i] + assertions[i].b);
    }
    return nets;
}

int assign_tracks_left_edge(std::vector<Net>& nets) {
    std::vector<Net*> unassigned;
    for (Net& n : nets) unassigned.push_back(&n);
    std::sort(unassigned.begin(), unassigned.end(),
              [](const Net* x, const Net* y) { return x->left < y->left; });

    int track = 0;
    while (!unassigned.empty()) {
        ++track;
        int watermark = -1;
        std::vector<Net*> rest;
        for (Net* n : unassigned) {
            if (n->left > watermark) {
                n->track = track;
                watermark = n->right;
            } else {
                rest.push_back(n);
            }
        }
        unassigned = std::move(rest);
    }
    return track;
}

}  // namespace sva
