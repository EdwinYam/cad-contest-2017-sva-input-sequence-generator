# SVA input sequence generator

`sva_gen` reads a finite state machine written in Verilog and a set of
concurrent SystemVerilog assertions. It writes an input sequence that makes as
many of the assertions fail as it can, and keeps that sequence short.

Rebuilt from our 2017 report for the Taiwan CAD Contest problem (NTU EDA course project); the original source was not preserved.

## Problem

The problem is "Input Sequence Generator for SystemVerilog Assertion Checking",
set by Chun-Ming Huang and Wei-Chang Tsai (CIC, Taiwan). Each benchmark is a
`module fsm(clk, rst, in, out)` Mealy machine. Its assertions look like this:

```systemverilog
assertion_rule1: assert property
    (@(posedge clk) $rose(in[1]) |-> ##[1:3] $rose(out))
    else $display("assertion rule1 failed");
```

The program has to produce an input sequence for simulation. Ranking goes by:

1. the number of assertions that fail at least once,
2. then the shortest sequence,
3. then the shortest runtime.

Some assertions cannot fail at all.

**Input format.** This follows the published problem statement. `fsm.v`
contains the FSM and its assertions. A separate assertion file is optional;
when you give one, the assertions are read from it instead of from `fsm.v`.

**Output format.** This also follows the published problem statement. Each
line is one clock cycle. The first character is `rst`, followed by the data
inputs from MSB to LSB. The sequence begins with one `rst = 0` line and then a
reset line, so that `rst` rises from 0 to 1 as the test bench requires.

```
000      idle (state still unknown)
100      reset -> S0
010      in[1] rises: rule1 triggers
000
000
000      no $rose(out) in cycles 1..3 after the trigger: rule1 fails
```

### Supported subset

- **Verilog**
  - Ports in plain or ANSI style, with `[N-1:0]` vectors.
  - `parameter` and `localparam`.
  - One clocked block: `if (rst) S <= RESET; else S <= NEXT;`.
  - Combinational `always @(*)` blocks made of assignments (including
    `x[i] = ...`), `if/else`, `case`, `casex` and `casez`, `?:` and the usual
    operators.
  - At most 16 data input bits.
- **SVA**
  - `A |-> ##[a:b] C`, `A |-> ##n C`, `A |-> C` and `A |=> C`.
  - `A` and `C` can each be `$rose(x)`, `$fell(x)`, `$stable(x)`, `x` or `!x`,
    on any single input or output bit.

### Cycle model

This is the model used by the simulator (`Graph::verify_assertion_failure`)
and by the generator:

- On every line, the assertions sample the line's inputs and the Mealy
  output for the present state and those inputs.
- A line with `rst = 1` puts the machine in the start state for that cycle,
  and it is still there on the next cycle.
- Before the first reset, outputs are unknown (X). An unknown value never
  triggers an assertion and never decides one.
- An assertion counts as failed only when its whole window `T+a .. T+b` lies
  inside the sequence.
- Checks that are already running continue across a reset.

## How it works

The algorithm follows the report.

1. **Parse `fsm.v` into a Mealy graph** (`build_mealy`). Starting from the reset
   state, the combinational block is run once for every input vector of every
   state that has been reached so far. Input vectors that lead to the same
   (next state, output) pair are merged into one `Edge`. The edge's input
   condition is a small set of disjoint cubes such as `1x`, built by Shannon
   expansion. Running the block like this, instead of reading the `casex`
   patterns directly, handles overlapping items and their priority exactly.
   Only reachable states are ever created.
2. **Convert Mealy to Moore** (`mealy2moore`). An assertion asks whether an
   output bit rose, fell or stayed the same, so it needs both the previous
   output and the current one. Each Mealy state `m` is split into one Moore
   node per distinct output on its incoming edges. Every Moore edge then
   stores, for each output bit, whether it rises, falls or stays stable
   (`Edge::property`), read from the outputs of its head and tail nodes. The
   reset node is `(start state, output for all-zero input)`.
3. **Treat each assertion as a reachability problem** (`generate_one_sequence`).
   A search state is a Moore node together with the last values of the input
   bits that the assertion reads. Moore nodes alone do not record inputs, so
   `$rose(in[k])` needs these extra bits. The search then works in layers:
   - BFS from reset finds every reachable state and its distance, and keeps
     parent links for shortest paths.
   - `R0` holds the states entered by a step on which the antecedent fires.
     They are scanned in BFS order, so the first trigger found is the closest
     one.
   - `R1 .. Ra-1` are free steps.
   - `Ra .. Rb` only allow steps on which the consequent does **not** hold.
   - If `Rb` is empty, the assertion is proven unfailable.
4. **Back-trace.** Start from the first state in `Rb` and walk back through the
   layers to the trigger, then along the BFS parents to reset. Each edge is
   then turned into a concrete input line: the tracked bits get the values the
   search chose, and every other don't-care bit becomes 0. The resulting piece
   has `1 + k + b` lines, where `k` is the trigger cycle. On the 2-input
   example, a unit test checks against brute force that this is the shortest
   possible piece.
5. **Build the full sequence** (`run_method`) using one of three methods:
   - **A**: generate a piece for every assertion and concatenate the pieces.
     Each piece starts with a reset.
   - **B**: as A, but after each piece, simulate it against the assertions
     still pending and skip any that it already fails.
   - **C**: as B, but visit the assertions by priority, largest termination
     time `b` first, with ties broken by smaller `a`. A long window often
     fails shorter ones along the way, while a short window cannot cover a
     longer one. Domination is also used here. Assertion X dominates Y when
     both have the same antecedent and consequent and X's window `[a, b]`
     contains Y's. Once X has failed, Y is marked as failed without any
     simulation.
6. The reported failure count always comes from a final simulation of the
   whole sequence on the Mealy graph. That simulation is independent of the
   Moore graph and the search.

**Channel-routing view (partial).** `sva_gen --plan` prints the report's
analysis, which treats assertion ordering as channel routing. It contains:

- the pairwise rules for when two assertions "do not fulfil each other"
  (rise/fall on different bits, or time intervals that do not overlap),
- greedy grouping of assertions into nets,
- left-edge assignment of the nets to tracks.

It is analysis only and does not change the sequence. The vertical constraint
graph, doglegs and merging a whole track into one sequence are not
implemented.

## Build and run

You need CMake 3.16 or newer and a C++17 compiler. The tests use GoogleTest.
CMake uses an installed copy if it finds one and otherwise downloads v1.14.0.

```sh
cmake -S . -B build
cmake --build build -j
ctest --test-dir build            # 34 tests

./build/sva_gen benchmarks/handshake/fsm.v benchmarks/handshake/assertions.sva \
    -o sequence.txt --method C
./build/sva_gen -i benchmarks/contest_example/fsm.v -o input_sequence   # contest form
```

Example output:

```
fsm: 6 Mealy states, 14 Mealy edges -> 12 Moore nodes (12 reachable), 27 Moore edges
method C: 8/10 assertions failed, sequence length 31, 6 pieces generated, 2 proven unfailable
runtime: parse+build 0.171 ms, generate 0.044 ms, total 0.215 ms
  [FAIL] h1_req_ack       $rose(req) |-> ##[1:3] $rose(ack)
  ...
```

The default method is C. Add `--plan` to print the net and track analysis.

## Benchmarks and results

| Benchmark | What it is |
|---|---|
| `contest_example` | The 3-state FSM from the problem statement. It has 5 embedded assertions, one of them dominated and one that cannot fail. |
| `handshake` | Request/grant controller written with `if/else`. It has 4 one-bit inputs, 4 one-bit outputs and 10 assertions. |
| `traffic_light` | Two-way light with emergency override, written with `casex` on packed vectors. It has 10 assertions. |
| `rand_m` | Random FSM with the size of public case 1: 36 states, 7 inputs, 7 outputs, 10 assertions. |
| `rand_l` | Random FSM with the size of hidden case 5: 146 states, 8 inputs, 19 outputs, 20 assertions. |

To make a larger random benchmark, run `scripts/gen_random_fsm.py`. To
regenerate the table, run `scripts/run_benchmarks.py`.

The table below was **measured on this implementation**. The machine was an
Apple M1 Pro running macOS 14.6.1, compiled with Apple clang 15.0.0 in a
Release build. Times are the median of 5 runs. "Build" covers parsing plus
building both graphs, and is the same for every method. "Generate" is the
method itself.

| Benchmark | Mealy / Moore states | Assertions | Method | Failed | Proven unfailable | Sequence length | Pieces generated | Build (ms) | Generate (ms) |
|---|---|---|---|---|---|---|---|---|---|
| contest_example | 3 / 3 | 5 | A | 4 | 1 | 18 | 4 | 0.13 | 0.017 |
| contest_example | 3 / 3 | 5 | B | 4 | 1 | 11 | 2 | 0.13 | 0.014 |
| contest_example | 3 / 3 | 5 | C | 4 | 1 | 11 | 2 | 0.12 | 0.013 |
| handshake | 6 / 12 | 10 | A | 8 | 2 | 41 | 8 | 0.17 | 0.041 |
| handshake | 6 / 12 | 10 | B | 8 | 2 | 31 | 6 | 0.18 | 0.043 |
| handshake | 6 / 12 | 10 | C | 8 | 2 | 31 | 6 | 0.17 | 0.044 |
| traffic_light | 9 / 11 | 10 | A | 8 | 2 | 44 | 8 | 0.23 | 0.047 |
| traffic_light | 9 / 11 | 10 | B | 8 | 2 | 39 | 7 | 0.22 | 0.053 |
| traffic_light | 9 / 11 | 10 | C | 8 | 2 | 30 | 5 | 0.21 | 0.039 |
| rand_m | 36 / 89 | 10 | A | 10 | 0 | 76 | 10 | 2.71 | 0.257 |
| rand_m | 36 / 89 | 10 | B | 10 | 0 | 57 | 7 | 2.86 | 0.228 |
| rand_m | 36 / 89 | 10 | C | 10 | 0 | 57 | 7 | 2.71 | 0.206 |
| rand_l | 146 / 406 | 20 | A | 20 | 0 | 150 | 20 | 50.98 | 3.809 |
| rand_l | 146 / 406 | 20 | B | 20 | 0 | 95 | 11 | 50.21 | 2.479 |
| rand_l | 146 / 406 | 20 | C | 20 | 0 | 89 | 10 | 51.09 | 2.365 |
| rand_xl* | 400 / 1186 | 60 | A | 60 | 0 | 394 | 60 | 6108.80 | 27.079 |
| rand_xl* | 400 / 1186 | 60 | B | 60 | 0 | 189 | 24 | 6002.69 | 11.801 |
| rand_xl* | 400 / 1186 | 60 | C | 60 | 0 | 136 | 17 | 5886.52 | 9.098 |

\* This benchmark is not committed. Make it with
`scripts/gen_random_fsm.py --states 400 --inputs 12 --outputs 24 --assertions 60 --seed 9 -o /tmp/rand_xl`.

What the numbers show:

- All three methods fail the same assertions. Every assertion that was not
  failed has been proven unfailable, so no method misses a failable one.
- B and C mostly differ from A in sequence length: B is 11–52% shorter, and C
  is 24–65% shorter.
- C beats B when windows overlap. On `rand_xl`, C needs 17 pieces, B needs 24
  and A needs 60.
- Generation time drops with the number of pieces (27 → 9 ms on `rand_xl`).
- On large inputs, building the graphs costs far more than generating. That
  is because the Mealy graph is built by evaluating every state on every
  input vector.

The 2017 report measured 6.5 s, 3.3 s and 0.5 s for methods A, B and C on the
contest benchmarks. Those benchmarks can no longer be downloaded, so the
numbers above are not directly comparable.

## Differences from the report

- **Input bits in the search.** The report's search state is a Moore node.
  Here it is a Moore node plus the input bits the assertion reads, so that
  rises and falls on input bits are exact.
- **Building the Mealy graph.** The report read the `casex` patterns directly.
  This version runs the block on every input vector and then compresses the
  results into cubes, which gets overlapping items right.
- **Reset node.** `mealy2moore` adds one more Moore node for the reset cycle,
  because the output while in reset may not match any incoming edge.
- **Channel routing.** The channel-routing and constrained left-edge ordering
  are only an analysis here (`--plan`). They are not used to build sequences.
- **Resets inside a sequence.** The generator never uses a reset in the middle
  of a piece to fail an assertion. A sequence that only fails with such a
  reset is not found, and that assertion is then reported as unfailable.

## Layout

```
src/lexer.*           tokenizer, number literals, token stream
src/verilog_ast.*     statement/expression AST and interpreter
src/verilog_parser.*  FSM subset parser -> FsmModule
src/assertion.*       SVA conditions, assertions, signal table, dominance
src/graph.*           Node / Edge / Graph, build_mealy, mealy2moore, simulator
src/reachability.*    generate_one_sequence (layers + back-trace)
src/methods.*         methods A, B, C and the priority order
src/left_edge.*       net grouping and left-edge tracks (analysis)
src/main.cpp          command-line tool
tests/                GoogleTest unit tests
benchmarks/           FSM + assertion benchmarks
scripts/              random benchmark generator, benchmark table
```
