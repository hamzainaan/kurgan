<p align="center">
  <img src="logo/logo256.png" alt="Kurgan" width="200">
</p>

<p align="center">
  <a href="https://github.com/hamzainaan/kurgan/actions/workflows/build.yml">
    <img src="https://github.com/hamzainaan/kurgan/actions/workflows/build.yml/badge.svg?branch=main&event=push" alt="Build">
  </a>
  <a href="https://github.com/hamzainaan/kurgan/actions/workflows/build.yml">
    <img src="https://img.shields.io/github/actions/workflow/status/hamzainaan/kurgan/build.yml?branch=main&event=push&label=CI&logo=githubactions&logoColor=white&style=flat-square" alt="CI">
  </a>
  <a href="https://github.com/hamzainaan/kurgan/actions/workflows/build.yml">
    <img src="https://img.shields.io/github/actions/workflow/status/hamzainaan/kurgan/build.yml?branch=main&label=main&logo=git&logoColor=white&style=flat-square" alt="main build">
  </a>
</p>

# Kurgan

A UCI chess engine, written in C++23, mostly to see whether I could. Turns out I could, eventually.

Maintained in the quiet hope that it stops losing to Stockfish. It will not.

## Features

- Bitboards; magic bitboards for the sliders.
- Alpha-beta, iterative deepening, aspiration windows.
- Transposition table, killers, history, LMR, null move.
- Assorted pruning: reverse futility, futility, SEE-based quiet pruning.
- Lazy SMP, tapered evaluation.
- Perft and a deterministic benchmark.

## Build

Needs a C++23 compiler.

```
make
```

Debug with `make MODE=debug`. PGO, if you insist: `make MODE=profile-gen && make bench && make MODE=profile-use`.

## Usage

It speaks UCI. Point a GUI at it, or drive it from a terminal:

```
perft <depth>
perft divide <depth>
perft suite
bench [depth]
```

`make test` runs the perft suite and the benchmark. If perft fails, the engine is wrong and nothing else matters. The benchmark's node count is the reference for deciding whether your clever idea was, in fact, clever.

## Thanks

To the authors of the open source engines I have read, and to the Chess Programming Wiki, which has answered most of my stupid questions without complaining. What bugs remain are mine.

## License

See `LICENSE`.