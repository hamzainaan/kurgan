#pragma once

#include <cstdint>
#include <string>

// Search instrumentation.
namespace stats
{
    struct Tally
    {
        // Nodes
        uint64_t nodes = 0;
        uint64_t qnodes = 0;

        // Transposition table
        uint64_t ttProbes = 0;
        uint64_t ttHits = 0;
        uint64_t ttExact = 0;
        uint64_t ttCutoffs = 0;
        uint64_t ttCollisions = 0;
        uint64_t ttStoresExact = 0;
        uint64_t ttStoresLower = 0;
        uint64_t ttStoresUpper = 0;

        // Move ordering
        uint64_t listNodes = 0;   // nodes that walked their move list
        uint64_t cutNodes = 0;    // nodes whose list ended in a beta cutoff
        uint64_t ttMoveCutoffs = 0;
        uint64_t firstCutoffs = 0;
        uint64_t earlyCutoffs = 0; // cutoff on one of the first three moves
        uint64_t movesSearched = 0;

        // Late move reduction
        uint64_t lmrCount = 0;
        uint64_t lmrReduction = 0; // sum of the reductions
        uint64_t lmrFailHigh = 0;  // reduced search beat alpha
        uint64_t lmrResearch = 0;  // verification re-search at full depth
        uint64_t lmrVerified = 0;  // ... and the full-depth search beat alpha too
        uint64_t lmrRefuted = 0;   // ... the reduction invented the fail-high
        uint64_t lmrResearchNodes = 0; // nodes spent inside those verification searches

        // Internal iterative deepening
        uint64_t iidCount = 0;
        uint64_t iidNodes = 0; // nodes spent inside the shallower search

        // Pruning
        uint64_t nmpEligible = 0;     // nodes whose gate passed, before the eval test
        uint64_t nmpCutoffs = 0;
        uint64_t rfpCutoffs = 0;
        uint64_t razorCutoffs = 0;
        uint64_t probcutEligible = 0; // nodes that scanned their captures
        uint64_t probcutCutoffs = 0;
        uint64_t lmpPruned = 0;
        uint64_t futilityPruned = 0;
        uint64_t seeQuietPruned = 0;

        // Quiescence
        uint64_t seeRejects = 0;
        uint64_t qsearchCutoffs = 0;
        uint64_t standPatCutoffs = 0;

        // NNUE
        uint64_t evalCalls = 0;
        uint64_t evalRebuilds = 0; // half accumulators rebuilt from scratch
        uint64_t evalRefills = 0;  // half accumulators refilled from the king-square cache
        uint64_t evalUpdates = 0;  // feature rows applied incrementally
    };

    // Counters of the calling thread.
    Tally &current();

    // Hand over the finished thread's counters.
    void record(int worker, int depth, int completed);
    void clearRecords();

    // When reporting is on, every search prints its own table.
    bool reporting();
    void setReporting(bool value);

    // Aggregated table of everything recorded since clearRecords().
    std::string report();
    void print();
}
