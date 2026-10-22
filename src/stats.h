#pragma once

#include <cstdint>
#include <string>

namespace stats
{
    struct Tally
    {
        uint64_t nodes = 0;
        uint64_t qnodes = 0;

        uint64_t ttProbes = 0;
        uint64_t ttHits = 0;
        uint64_t ttExact = 0;
        uint64_t ttCutoffs = 0;
        uint64_t ttCollisions = 0;
        uint64_t ttStoresExact = 0;
        uint64_t ttStoresLower = 0;
        uint64_t ttStoresUpper = 0;

        uint64_t listNodes = 0;
        uint64_t cutNodes = 0;
        uint64_t ttMoveCutoffs = 0;
        uint64_t firstCutoffs = 0;
        uint64_t earlyCutoffs = 0; // Cutoffs within the first three moves.
        uint64_t movesSearched = 0;

        // LMR statistics distinguish verified fail-highs from refuted ones.
        uint64_t lmrCount = 0;
        uint64_t lmrReduction = 0; // Sum of all reductions.
        uint64_t lmrFailHigh = 0;
        uint64_t lmrResearch = 0;
        uint64_t lmrVerified = 0;
        uint64_t lmrRefuted = 0;
        uint64_t lmrResearchNodes = 0;

        uint64_t iidCount = 0;
        uint64_t iidNodes = 0;

        uint64_t nmpEligible = 0; // Nodes that passed the NMP gate.
        uint64_t nmpCutoffs = 0;
        uint64_t rfpCutoffs = 0;
        uint64_t razorCutoffs = 0;
        uint64_t probcutEligible = 0; // Nodes that scanned their captures.
        uint64_t probcutCutoffs = 0;
        uint64_t lmpPruned = 0;
        uint64_t futilityPruned = 0;
        uint64_t seeQuietPruned = 0;

        uint64_t seeRejects = 0;
        uint64_t qsearchCutoffs = 0;
        uint64_t standPatCutoffs = 0;

        uint64_t evalCalls = 0;
        uint64_t evalRebuilds = 0; // Half accumulators rebuilt from scratch.
        uint64_t evalRefills = 0; // Half accumulators refilled from the king cache.
        uint64_t evalUpdates = 0; // Feature rows applied incrementally.
    };

    // Returns the calling thread's counters.
    Tally &current();

    // Records a finished worker's counters.
    void record(int worker, int depth, int completed);
    void clearRecords();

    // True when each search prints its statistics table.
    bool reporting();
    void setReporting(bool value);

    // Returns the aggregate table accumulated since the last clearRecords call.
    std::string report();
    void print();
}
