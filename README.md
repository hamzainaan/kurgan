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