<p align="center">
  <img src="logo/logo256.png" alt="Kurgan" width="200">
</p>

<p align="center">
  <a href="https://github.com/hamzainaan/kurgan/releases/latest">
    <img src="https://img.shields.io/github/v/release/hamzainaan/kurgan?label=release&logo=github&logoColor=white&style=flat-square" alt="Release">
  </a>
  <a href="https://github.com/hamzainaan/kurgan/actions/workflows/build.yml">
    <img src="https://img.shields.io/github/actions/workflow/status/hamzainaan/kurgan/build.yml?event=pull_request&label=build&logo=githubactions&logoColor=white&style=flat-square" alt="Build">
  </a>
</p>

<p align="center">
  <a href="https://lichess.org/@/KurganUCI/perf/bullet">
    <img src="https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Flichess.org%2Fapi%2Fuser%2FKurganUCI&query=%24.perfs.bullet.rating&label=bullet&logo=lichess&logoColor=white&color=informational&style=flat-square" alt="Lichess bullet rating">
  </a>
  <a href="https://lichess.org/@/KurganUCI/perf/blitz">
    <img src="https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Flichess.org%2Fapi%2Fuser%2FKurganUCI&query=%24.perfs.blitz.rating&label=blitz&logo=lichess&logoColor=white&color=informational&style=flat-square" alt="Lichess blitz rating">
  </a>
  <a href="https://lichess.org/@/KurganUCI/perf/rapid">
    <img src="https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Flichess.org%2Fapi%2Fuser%2FKurganUCI&query=%24.perfs.rapid.rating&label=rapid&logo=lichess&logoColor=white&color=informational&style=flat-square" alt="Lichess rapid rating">
  </a>
  <a href="https://lichess.org/@/KurganUCI/perf/chess960">
    <img src="https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Flichess.org%2Fapi%2Fuser%2FKurganUCI&query=%24.perfs.chess960.rating&label=chess960&logo=lichess&logoColor=white&color=informational&style=flat-square" alt="Lichess chess960 rating">
  </a>
</p>

# Kurgan

A UCI chess engine, written in C++23, mostly to see whether I could. Turns out I could, eventually.

Maintained in the quiet hope that it stops losing to Stockfish. It will not.

## Features

- Bitboards; magic bitboards for the sliders.
- Alpha-beta with PVS, iterative deepening, aspiration windows, quiescence search.
- Transposition table: four-entry clusters per cache line, depth and age replacement, prefetching.
- Move ordering: staged move picker, killers, counter moves, history, capture history,
  continuation history one, two, four and six plies back.
- Assorted pruning: reverse futility, razoring, null move, ProbCut, late move pruning, futility,
  history and SEE-based quiet pruning, mate distance pruning.
- LMR, internal iterative reduction, singular extensions (double and negative too), check extensions.
- Static evaluation correction history keyed by pawn and non-pawn structure.
- Lazy SMP with staggered helper depths.
- Default NNUE evaluation (HCE optional): mirrored king-bucket HalfKA (24576
  inputs, 512-wide SCReLU hidden layer, 8 output buckets), trained with Bullet.
  The net is embedded in the binary.
- Tapered hand-crafted evaluation for when you turn the net off.
- Endgame knowledge: a KPK bitbase, dedicated evaluations for the usual suspects (KBNK, KRKP, KQKR
  and friends), and scaling rules for drawish endings.
- Time management that spends less when the best move dominates the search and more when the
  position is tactical or the score drops.
- Instant moves: a forced reply, or a recapture that a verification search confirms beats every
  alternative by a wide margin, is played at once like a human would (`Instant Moves` option).
- UCI extras: MultiPV, pondering, searchmoves, contempt, move overhead, analysis mode, refutations.
- Perft and a deterministic benchmark.
- Self-play data generation (`datagen`) in bulletformat, for training the next net.
- FRC960 (Fischer Random).

## Build

Needs a C++23 compiler, g++ or clang++. The net is linked into the binary with `.incbin`, so MSVC is out; on Windows, MSYS2 with MinGW g++ works.

```
make
```

That builds for the CPU you are sitting at (`TIER=native`). For a binary that has to run somewhere else, pick a tier: `make TIER=avx2`. The tiers are `baseline`, `sse42`, `avx2`, `avx512`, `neoverse` (ARM servers) and `apple` (Apple Silicon).

Debug with `make MODE=debug`. PGO, if you insist: `make MODE=profile-gen bench && make MODE=profile-use`. `VERSION=3` sets the name the engine reports, and `make clean` throws everything away.

Prebuilt binaries for Linux, Windows, macOS and Android are on the [releases page](https://github.com/hamzainaan/kurgan/releases).

## Usage

It speaks UCI. Point a GUI at it, or drive it from a terminal.

`make test` runs the perft suite and the benchmark. If perft fails, the engine is wrong and nothing else matters. The benchmark's node count is the reference for deciding whether your clever idea was, in fact, clever.

## Thanks

To the authors of the open source engines I have read, and to the Chess Programming Wiki, which has answered most of my stupid questions without complaining. What bugs remain are mine.

## License

See `LICENSE`.