// Assertion grouping viewed as channel routing (report, Sec. IV-B).
//
// PARTIAL IMPLEMENTATION. This is an analysis printed by `sva_gen --plan`;
// it does not change the generated sequence. Implemented: the pairwise
// "do not fulfil each other" rules, greedy grouping of assertions into nets,
// and left-edge assignment of nets to tracks. Not implemented: the vertical
// constraint graph, doglegs for constraint cycles, and building one merged
// input sequence per track.
#pragma once

#include <vector>

#include "assertion.h"

namespace sva {

struct Net {
    std::vector<size_t> members;  // assertion indices
    int left = 0;                 // smallest trigger cycle k of the members
    int right = 0;                // largest k + b of the members
    int track = 0;                // 1-based track from left-edge assignment
};

// Report rules 1-3: x and y can share a net when neither one's trigger can
// satisfy the other's consequent, or when their time intervals [k, k+b] are
// disjoint. kx/ky are the trigger cycles from generate_one_sequence.
bool compatible(const Assertion& x, int kx, const Assertion& y, int ky);

// Places each assertion (k[i] >= 0) into the first net whose members are all
// compatible with it, opening a new net otherwise. `order` is the visit order.
std::vector<Net> group_into_nets(const std::vector<Assertion>& assertions, const std::vector<int>& k,
                                 const std::vector<size_t>& order);

// Unconstrained left-edge algorithm: fills tracks one at a time, always taking
// the unassigned net with the smallest left end beyond the watermark.
// Returns the number of tracks used.
int assign_tracks_left_edge(std::vector<Net>& nets);

}  // namespace sva
