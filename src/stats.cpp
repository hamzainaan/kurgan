#include "stats.h"

#include <algorithm>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <vector>

namespace
{
    thread_local stats::Tally tally;

    struct Record
    {
        int worker;
        int depth;
        int completed;
        stats::Tally tally;
    };

    std::mutex recordMutex;
    std::vector<Record> records;
    bool reportingEnabled = false;

    double percent(uint64_t part, uint64_t whole)
    {
        return whole == 0 ? 0.0 : 100.0 * static_cast<double>(part) / static_cast<double>(whole);
    }
}

stats::Tally &stats::current()
{
    return tally;
}

void stats::record(int worker, int depth, int completed)
{
    std::lock_guard<std::mutex> lock(recordMutex);
    records.push_back({worker, depth, completed, tally});
}

void stats::clearRecords()
{
    std::lock_guard<std::mutex> lock(recordMutex);
    records.clear();
}

bool stats::reporting()
{
    return reportingEnabled;
}

void stats::setReporting(bool value)
{
    reportingEnabled = value;
}

std::string stats::report()
{
    std::vector<Record> local;
    {
        std::lock_guard<std::mutex> lock(recordMutex);
        local = records;
    }

    if (local.empty())
        return "";

    Tally t;
    for (const Record &r : local)
    {
        const Tally &s = r.tally;
        t.nodes += s.nodes;
        t.qnodes += s.qnodes;
        t.ttProbes += s.ttProbes;
        t.ttHits += s.ttHits;
        t.ttExact += s.ttExact;
        t.ttCutoffs += s.ttCutoffs;
        t.ttCollisions += s.ttCollisions;
        t.ttStoresExact += s.ttStoresExact;
        t.ttStoresLower += s.ttStoresLower;
        t.ttStoresUpper += s.ttStoresUpper;
        t.listNodes += s.listNodes;
        t.cutNodes += s.cutNodes;
        t.ttMoveCutoffs += s.ttMoveCutoffs;
        t.firstCutoffs += s.firstCutoffs;
        t.earlyCutoffs += s.earlyCutoffs;
        t.movesSearched += s.movesSearched;
        t.lmrCount += s.lmrCount;
        t.lmrReduction += s.lmrReduction;
        t.lmrFailHigh += s.lmrFailHigh;
        t.lmrResearch += s.lmrResearch;
        t.lmrVerified += s.lmrVerified;
        t.lmrRefuted += s.lmrRefuted;
        t.lmrResearchNodes += s.lmrResearchNodes;
        t.iidCount += s.iidCount;
        t.iidNodes += s.iidNodes;
        t.nmpEligible += s.nmpEligible;
        t.nmpCutoffs += s.nmpCutoffs;
        t.rfpCutoffs += s.rfpCutoffs;
        t.razorCutoffs += s.razorCutoffs;
        t.probcutEligible += s.probcutEligible;
        t.probcutCutoffs += s.probcutCutoffs;
        t.lmpPruned += s.lmpPruned;
        t.futilityPruned += s.futilityPruned;
        t.seeQuietPruned += s.seeQuietPruned;
        t.seeRejects += s.seeRejects;
        t.qsearchCutoffs += s.qsearchCutoffs;
        t.standPatCutoffs += s.standPatCutoffs;
        t.evalCalls += s.evalCalls;
        t.evalRebuilds += s.evalRebuilds;
        t.evalUpdates += s.evalUpdates;
    }

    std::ostringstream out;
    out.setf(std::ios::fixed);

    out << "nodes      " << t.nodes << " (alpha-beta " << (t.nodes - t.qnodes) << ", qsearch " << t.qnodes << ")"
        << "  searches " << local.size() << "\n";

    out.precision(1);
    out << "TT         probes " << t.ttProbes
        << "  hits " << t.ttHits << " (" << percent(t.ttHits, t.ttProbes) << "%)"
        << "  exact " << t.ttExact << " (" << percent(t.ttExact, t.ttProbes) << "%)"
        << "  cutoffs " << t.ttCutoffs
        << "  collisions " << t.ttCollisions << " (" << percent(t.ttCollisions, t.ttProbes) << "%)\n";

    const uint64_t stores = t.ttStoresExact + t.ttStoresLower + t.ttStoresUpper;
    out << "TT store   " << stores
        << "  exact " << percent(t.ttStoresExact, stores) << "%"
        << "  lower " << percent(t.ttStoresLower, stores) << "%"
        << "  upper " << percent(t.ttStoresUpper, stores) << "%\n";

    out << "ordering   list nodes " << t.listNodes
        << "  cut nodes " << t.cutNodes
        << "  tt-move cutoffs " << percent(t.ttMoveCutoffs, t.cutNodes) << "%"
        << "  first-move " << percent(t.firstCutoffs, t.cutNodes) << "%"
        << "  first-3 " << percent(t.earlyCutoffs, t.cutNodes) << "%";
    out.precision(2);
    out << "  avg searched " << (t.listNodes == 0 ? 0.0 : static_cast<double>(t.movesSearched) / static_cast<double>(t.listNodes)) << "\n";

    out.precision(1);
    out << "LMR        count " << t.lmrCount
        << "  avg reduction " << (t.lmrCount == 0 ? 0.0 : static_cast<double>(t.lmrReduction) / static_cast<double>(t.lmrCount))
        << "  fail-high " << t.lmrFailHigh << " (" << percent(t.lmrFailHigh, t.lmrCount) << "%)"
        << "  re-search " << t.lmrResearch << " (" << percent(t.lmrResearch, t.lmrCount) << "%)"
        << "  verified " << t.lmrVerified << " (" << percent(t.lmrVerified, t.lmrResearch) << "%)"
        << "  refuted " << t.lmrRefuted << " (" << percent(t.lmrRefuted, t.lmrResearch) << "%)"
        << "  re-search nodes " << t.lmrResearchNodes << " (" << percent(t.lmrResearchNodes, t.nodes) << "% of all)\n";

    out << "IID        count " << t.iidCount
        << "  nodes " << t.iidNodes << " (" << percent(t.iidNodes, t.nodes) << "% of all)\n";

    // The last three count pruned MOVES, everything else counts nodes.
    out << "pruning    nmp " << t.nmpCutoffs << " (elig " << t.nmpEligible << ")"
        << "  rfp " << t.rfpCutoffs
        << "  razor " << t.razorCutoffs
        << "  probcut " << t.probcutCutoffs << " (nodes " << t.probcutEligible << ")"
        << "  lmp " << t.lmpPruned << " moves"
        << "  move-futility " << t.futilityPruned << " moves"
        << "  see-quiet " << t.seeQuietPruned << " moves"
        << "\n";

    out << "qsearch    nodes " << t.qnodes
        << "  see rejects " << t.seeRejects
        << "  beta cutoffs " << t.qsearchCutoffs
        << "  stand-pat cutoffs " << t.standPatCutoffs << "\n";

    out.unsetf(std::ios::fixed);
    out << "NNUE       eval calls " << t.evalCalls
        << "  rebuilds " << t.evalRebuilds
        << "  incremental updates " << t.evalUpdates << "\n";

    struct WorkerSummary
    {
        uint64_t nodes = 0;
        uint64_t qnodes = 0;
        int depth = 0;
        int completed = 0;
        int searches = 0;
    };

    std::map<int, WorkerSummary> perWorker;
    for (const Record &r : local)
    {
        WorkerSummary &w = perWorker[r.worker];
        w.nodes += r.tally.nodes;
        w.qnodes += r.tally.qnodes;
        w.depth = std::max(w.depth, r.depth);
        w.completed = std::max(w.completed, r.completed);
        ++w.searches;
    }

    for (const auto &[worker, w] : perWorker)
        out << "SMP        worker " << worker
            << "  searches " << w.searches
            << "  nodes " << w.nodes << " (qsearch " << w.qnodes << ")"
            << "  seldepth " << w.depth
            << "  completed depth " << w.completed << "\n";

    return out.str();
}

void stats::print()
{
    const std::string text = report();
    if (text.empty())
        return;

    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line))
        std::cout << "info string stats " << line << std::endl;
}
