#include "uci.h"

#include "bench.h"
#include "datagen.h"
#include "evaluate.h"
#include "movegen.h"
#include "nnue.h"
#include "perft.h"
#include "position.h"
#include "search.h"
#include "stats.h"
#include "version.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
    struct Opponent
    {
        std::string title = "none";
        int elo = 0;
        bool computer = true;
        std::string name;
    };

    Opponent opponent;
    bool debugMode = false;

    std::string asciiLower(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(),
                       [](unsigned char c)
                       { return static_cast<char>(std::tolower(c)); });
        return text;
    }

    bool checkValue(const std::string &value)
    {
        const std::string lowered = asciiLower(value);
        return lowered == "true" || lowered == "1";
    }

    bool parseInteger(const std::string &token, int &value)
    {
        try
        {
            size_t used = 0;
            const int parsed = std::stoi(token, &used);
            if (used != token.size())
                return false;
            value = parsed;
            return true;
        }
        catch (const std::exception &)
        {
            return false;
        }
    }

    bool isGoKeyword(const std::string &token)
    {
        static const char *const keywords[] = {
            "searchmoves", "ponder", "wtime", "btime", "winc", "binc",
            "movestogo", "depth", "nodes", "mate", "movetime", "infinite"};
        for (const char *keyword : keywords)
            if (token == keyword)
                return true;
        return false;
    }

    void setOpponent(const std::string &value)
    {
        if (value.empty() || value == "<empty>")
        {
            opponent = Opponent{};
            return;
        }

        std::istringstream in(value);
        std::string title;
        std::string elo;
        std::string kind;
        if (!(in >> title >> elo >> kind))
            return;

        if (title != "GM" && title != "IM" && title != "FM" && title != "WGM" && title != "WIM" && title != "none")
            return;

        int rating = 0;
        if (elo != "none")
        {
            try
            {
                rating = std::stoi(elo);
            }
            catch (const std::exception &)
            {
                return;
            }
            if (rating < 0)
                return;
        }

        if (kind != "computer" && kind != "human")
            return;

        std::string name;
        std::getline(in, name);
        if (!name.empty() && name.front() == ' ')
            name.erase(0, 1);

        opponent.title = title;
        opponent.elo = rating;
        opponent.computer = kind == "computer";
        opponent.name = name;
    }

    // Matches a UCI move string against the pseudo-legal move list.
    Move parseMove(const Position &pos, const std::string &s)
    {
        if (s.size() != 4 && s.size() != 5)
            return Move();

        const int fromFile = s[0] - 'a';
        const int fromRank = s[1] - '1';
        const int toFile = s[2] - 'a';
        const int toRank = s[3] - '1';

        if (fromFile < 0 || fromFile > 7 || fromRank < 0 || fromRank > 7 ||
            toFile < 0 || toFile > 7 || toRank < 0 || toRank > 7)
            return Move();

        const Square from = makeSquare(static_cast<File>(fromFile), static_cast<Rank>(fromRank));
        const Square to = makeSquare(static_cast<File>(toFile), static_cast<Rank>(toRank));

        PieceType promo = NO_PIECE_TYPE;
        if (s.size() == 5)
        {
            switch (static_cast<char>(std::tolower(static_cast<unsigned char>(s[4]))))
            {
            case 'n':
                promo = KNIGHT;
                break;
            case 'b':
                promo = BISHOP;
                break;
            case 'r':
                promo = ROOK;
                break;
            case 'q':
                promo = QUEEN;
                break;
            default:
                return Move();
            }
        }

        MoveList list;
        movegen::generate_pseudo_legal_moves(pos, list);
        for (int i = 0; i < list.size; ++i)
        {
            const Move m = list.moves[i];
            if (m.from() != from)
                continue;

            if (!movegen::is_legal(pos, m))
                continue;

            if (m.isCastling())
            {
                if (promo != NO_PIECE_TYPE)
                    continue;
                if (movegen::chess960() ? m.to() == to
                                        : (to == castlingKingTo(m) || to == m.to()))
                    return m;
                continue;
            }

            if (m.to() != to)
                continue;
            if (m.isPromotion())
            {
                if (m.promoType() == promo)
                    return m;
            }
            else if (promo == NO_PIECE_TYPE)
            {
                return m;
            }
        }
        return Move();
    }

    void parsePosition(std::istringstream &ss, Position &pos)
    {
        std::string token;
        ss >> token;

        if (token == "startpos")
        {
            pos.set_fen(START_FEN);
        }
        else if (token == "fen")
        {
            std::string fen;
            for (int i = 0; i < 4; ++i)
            {
                std::string part;
                if (!(ss >> part))
                {
                    std::cout << "info string invalid fen '" << fen << '\'' << std::endl;
                    return;
                }
                if (i > 0)
                    fen += ' ';
                fen += part;
            }

            for (int i = 0; i < 2; ++i)
            {
                ss >> std::ws;
                const int next = ss.peek();
                if (next == EOF || next < '0' || next > '9')
                    break;
                std::string part;
                ss >> part;
                fen += ' ';
                fen += part;
            }

            Position parsed = pos;
            if (!parsed.set_fen(fen))
            {
                std::cout << "info string invalid fen '" << fen << '\'' << std::endl;
                return;
            }
            pos = parsed;
        }
        else
        {
            return;
        }

        ss >> token;
        if (token == "moves")
        {
            std::string moveStr;
            while (ss >> moveStr)
            {
                const Move m = parseMove(pos, moveStr);
                if (m.from() == m.to())
                {
                    std::cout << "info string invalid move '" << moveStr << '\'' << std::endl;
                    break;
                }
                if (!pos.do_move(m))
                {
                    std::cout << "info string illegal move '" << moveStr << '\'' << std::endl;
                    break;
                }
            }
        }
    }

    search::SearchLimits parseGo(std::istringstream &ss, const Position &pos)
    {
        search::SearchLimits limits;
        std::vector<std::string> tokens;
        std::string token;
        while (ss >> token)
            tokens.push_back(token);

        int requestedSearchMoves = 0;
        for (size_t i = 0; i < tokens.size(); ++i)
        {
            const std::string &current = tokens[i];
            int value = 0;

            if (current == "ponder")
                limits.ponder = true;
            else if (current == "infinite")
                limits.infinite = true;
            else if (current == "searchmoves")
            {
                while (i + 1 < tokens.size() && !isGoKeyword(tokens[i + 1]))
                {
                    ++i;
                    ++requestedSearchMoves;
                    const Move m = parseMove(pos, tokens[i]);
                    if (m.from() == m.to())
                        std::cout << "info string invalid searchmoves '" << tokens[i] << '\'' << std::endl;
                    else
                        limits.searchmoves.push_back(m);
                }
            }
            else if (i + 1 < tokens.size() && parseInteger(tokens[i + 1], value))
            {
                if (current == "depth")
                    limits.depth = std::max(1, value);
                else if (current == "movetime")
                    limits.movetime = std::max(1, value);
                else if (current == "wtime")
                    limits.wtime = std::max(1, value);
                else if (current == "btime")
                    limits.btime = std::max(1, value);
                else if (current == "winc")
                    limits.winc = std::max(0, value);
                else if (current == "binc")
                    limits.binc = std::max(0, value);
                else if (current == "movestogo")
                    limits.movestogo = std::max(0, value);
                else if (current == "nodes")
                    limits.nodes = std::max(1, value);
                else if (current == "mate")
                    limits.mate = std::max(0, value);
                else
                    continue;

                ++i;
            }
        }

        if (requestedSearchMoves > 0 && limits.searchmoves.empty())
            std::cout << "info string no legal searchmoves, searching all moves" << std::endl;

        return limits;
    }
}

void uci::loop()
{
    search::init();

    nnue::loadEmbedded();

    Position pos;
    pos.set_fen(START_FEN);

    std::thread searchThread;
    bool searchActive = false;

    auto joinSearch = [&]()
    {
        if (searchActive)
        {
            searchThread.join();
            searchActive = false;
        }
    };

    auto searchRunning = [&]()
    {
        return searchActive && search::isRunning();
    };

    std::string line;
    while (std::getline(std::cin, line))
    {
        // Windows GUIs may deliver CRLF while another OS uses LF (or 0x0a0c).
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        std::istringstream ss(line);
        std::string cmd;
        ss >> cmd;

        if (debugMode && !cmd.empty())
        {
            std::lock_guard<std::mutex> lock(search::outputLock());
            std::cout << "info string " << line << std::endl;
        }

        if (cmd == "uci")
        {
            std::lock_guard<std::mutex> lock(search::outputLock());
            std::cout << "id name Kurgan " << KURGAN_VERSION << std::endl;
            std::cout << "id author Hamza Inan" << std::endl;
            std::cout << "option name Threads type spin default 1 min " << search::THREADS_MIN
                      << " max " << search::maxThreadCount() << std::endl;
            std::cout << "option name Hash type spin default 16 min " << search::HASH_MIN
                      << " max " << search::HASH_MAX << std::endl;
            std::cout << "option name Clear Hash type button" << std::endl;
            std::cout << "option name MultiPV type spin default 1 min " << search::MULTIPV_MIN
                      << " max " << search::MULTIPV_MAX << std::endl;
            std::cout << "option name Ponder type check default false" << std::endl;
            std::cout << "option name Contempt type spin default 0 min " << search::CONTEMPT_MIN
                      << " max " << search::CONTEMPT_MAX << std::endl;
            std::cout << "option name Move Overhead type spin default " << search::MOVE_OVERHEAD_DEFAULT
                      << " min " << search::MOVE_OVERHEAD_MIN << " max " << search::MOVE_OVERHEAD_MAX << std::endl;
            std::cout << "option name UCI_Chess960 type check default false" << std::endl;
            std::cout << "option name UCI_AnalyseMode type check default false" << std::endl;
            std::cout << "option name UCI_ShowRefutations type check default false" << std::endl;
            std::cout << "option name UCI_Opponent type string default <empty>" << std::endl;
            std::cout << "option name Use NNUE type check default true" << std::endl;
            std::cout << "option name Search Stats type check default false" << std::endl;
            if (nnue::loaded())
                std::cout << "info string evaluation nnue (" << nnue::file() << ") id " << std::hex
                          << nnue::hash() << std::dec << std::endl;
            else
                std::cout << "info string evaluation " << evaluate::name() << " ("
                          << (nnue::supported() ? nnue::error() : "NNUE not compiled in") << ")" << std::endl;
            std::cout << "uciok" << std::endl;
        }
        else if (cmd == "isready")
        {
            std::lock_guard<std::mutex> lock(search::outputLock());
            std::cout << "readyok" << std::endl;
        }
        else if (cmd == "ucinewgame")
        {
            if (searchRunning())
                continue;
            joinSearch();
            search::clear();
        }
        else if (cmd == "setoption")
        {
            // UCI only sends setoption while the engine is waiting.
            if (searchRunning())
                continue;

            std::string name;
            std::string value;
            std::string token;
            bool inName = false;
            bool inValue = false;
            while (ss >> token)
            {
                if (token == "name")
                {
                    inName = true;
                    inValue = false;
                }
                else if (token == "value")
                {
                    inName = false;
                    inValue = true;
                }
                else if (inName)
                {
                    if (!name.empty())
                        name += ' ';
                    name += token;
                }
                else if (inValue)
                {
                    if (!value.empty())
                        value += ' ';
                    value += token;
                }
            }

            joinSearch();

            const std::string key = asciiLower(name);

            try
            {
                if (key == "hash")
                    search::setHashSize(std::stoi(value));
                else if (key == "threads")
                    search::setThreads(std::stoi(value));
                else if (key == "ponder")
                    search::setPonder(checkValue(value));
                else if (key == "uci_chess960")
                    movegen::setChess960(checkValue(value));
                else if (key == "multipv")
                    search::setMultiPV(std::stoi(value));
                else if (key == "contempt")
                    search::setContempt(std::stoi(value));
                else if (key == "uci_analysemode")
                    search::setAnalyseMode(checkValue(value));
                else if (key == "uci_showrefutations")
                    search::setShowRefutations(checkValue(value));
                else if (key == "uci_opponent")
                    setOpponent(value);
                else if (key == "move overhead")
                    search::setMoveOverhead(std::stoi(value));
                else if (key == "clear hash")
                    search::clear();
                else if (key == "use nnue")
                    nnue::setEnabled(checkValue(value));
                else if (key == "search stats")
                    stats::setReporting(checkValue(value));
                else if (!search::setTuningOption(name, std::stoi(value)))
                    std::cout << "info string unknown option " << name << std::endl;
            }
            catch (const std::exception &)
            {
                std::cout << "info string invalid value '" << value << "' for option " << name << std::endl;
            }
        }
        else if (cmd == "position")
        {
            if (searchRunning())
                continue;
            joinSearch();
            parsePosition(ss, pos);
        }
        else if (cmd == "go")
        {
            if (searchRunning())
                continue;
            joinSearch();
            const search::SearchLimits limits = parseGo(ss, pos);
            search::prepare(pos, limits);
            searchActive = true;
            searchThread = std::thread([pos, limits]()
                                       { search::go(pos, limits); });
        }
        else if (cmd == "stop")
        {
            // stop outside a search is ignored.
            if (searchRunning())
            {
                search::stop();
                joinSearch();
            }
            else
                joinSearch();
        }
        else if (cmd == "ponderhit")
        {
            if (searchRunning())
                search::ponderhit();
        }
        else if (cmd == "perft")
        {
            if (searchRunning())
                continue;
            joinSearch();

            std::string arg;
            ss >> arg;

            if (arg == "suite")
            {
                perft::suite();
            }
            else if (arg == "divide")
            {
                int depth = 1;
                ss >> depth;
                perft::divide(pos, depth);
            }
            else
            {
                int depth = 1;
                if (!arg.empty())
                {
                    std::istringstream ds(arg);
                    if (!(ds >> depth))
                        depth = 1;
                }

                const auto start = std::chrono::steady_clock::now();
                const uint64_t n = perft::nodes(pos, depth);
                const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::steady_clock::now() - start)
                                         .count();
                std::cout << "perft " << depth << " nodes " << n << " time " << ms << " ms";
                if (ms > 0)
                    std::cout << " nps " << (n * 1000 / static_cast<uint64_t>(ms));
                std::cout << std::endl;
            }
        }
        else if (cmd == "eval")
        {
            if (searchRunning())
                continue;
            joinSearch();
#ifdef KURGAN_HCE_OFF
            std::cout << "eval " << evaluate::evaluate(pos) << std::endl;
#else
            std::cout << "eval " << evaluate::hce(pos) << std::endl;
#endif
            if (nnue::loaded())
                std::cout << "eval nnue " << nnue::evaluate(pos) << std::endl;
            std::cout << "eval final " << evaluate::evaluate(pos) << std::endl;
        }
        else if (cmd == "debug")
        {
            std::string arg;
            ss >> arg;

            if (arg == "on")
            {
                if (!debugMode)
                {
                    debugMode = true;
                    std::lock_guard<std::mutex> lock(search::outputLock());
                    std::cout << "info string debug on" << std::endl;
                }
            }
            else if (arg == "off")
            {
                debugMode = false;
            }
            else if (arg.empty() && !searchRunning())
            {
                std::lock_guard<std::mutex> lock(search::outputLock());
                pos.print_board();
                std::cout << "Fen: " << pos.fen() << std::endl;
            }
        }
        else if (cmd == "bench")
        {
            if (searchRunning())
                continue;
            joinSearch();
            int depth = 0;
            ss >> depth;
            bench::run(depth);
        }
        else if (cmd == "datagen")
        {
            if (searchRunning())
                continue;
            joinSearch();
            datagen::Options options;
            std::string key;
            while (ss >> key)
            {
                if (key == "threads")
                    ss >> options.workers;
                else if (key == "positions")
                    ss >> options.positions;
                else if (key == "softnodes")
                    ss >> options.softNodes;
                else if (key == "hardnodes")
                    ss >> options.hardNodes;
                else if (key == "randomplies")
                    ss >> options.randomPlies;
                else if (key == "hash")
                    ss >> options.hash;
                else if (key == "seed")
                    ss >> options.seed;
                else if (key == "out")
                    ss >> options.out;
                else
                    std::cout << "info string unknown datagen option " << key << std::endl;
            }
            datagen::run(options);
        }
        else if (cmd == "quit")
        {
            if (searchRunning())
                search::stop();
            break;
        }
    }

    // End of input is equivalent to quit.
    search::stop();
    joinSearch();
}