#include "uci.h"

#include "bench.h"
#include "evaluate.h"
#include "movegen.h"
#include "nnue.h"
#include "perft.h"
#include "position.h"
#include "search.h"
#include "version.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

namespace
{
    constexpr const char *START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    constexpr const char *DEFAULT_NET = "net.knnue";

    // Convert a UCI move string ("e2e4", "e7e8q") into a Move by matching the
    // pseudo-legal move list of the current position.
    Move parseMove(const Position &pos, const std::string &s)
    {
        if (s.size() < 4)
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
        if (s.size() >= 5)
        {
            switch (s[4])
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
            default:
                promo = QUEEN;
                break;
            }
        }

        MoveList list;
        movegen::generate_pseudo_legal_moves(pos, list);
        for (int i = 0; i < list.size; ++i)
        {
            const Move m = list.moves[i];
            if (m.from() != from)
                continue;

            if (m.isCastling())
            {
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
            for (int i = 0; i < 6; ++i)
            {
                std::string part;
                if (!(ss >> part))
                    break;
                if (i > 0)
                    fen += ' ';
                fen += part;
            }
            pos.set_fen(fen);
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
                if (m.from() == m.to() || !pos.do_move(m))
                    break;
            }
        }
    }

    search::SearchLimits parseGo(std::istringstream &ss, const Position &pos)
    {
        search::SearchLimits limits;
        std::string token;
        while (ss >> token)
        {
            if (token == "depth")
                ss >> limits.depth;
            else if (token == "movetime")
                ss >> limits.movetime;
            else if (token == "wtime")
                ss >> limits.wtime;
            else if (token == "btime")
                ss >> limits.btime;
            else if (token == "winc")
                ss >> limits.winc;
            else if (token == "binc")
                ss >> limits.binc;
            else if (token == "nodes")
                ss >> limits.nodes;
            else if (token == "mate")
                ss >> limits.mate;
            else if (token == "ponder")
                limits.ponder = true;
            else if (token == "searchmoves")
            {
                std::string moveStr;
                while (ss >> moveStr)
                {
                    const Move m = parseMove(pos, moveStr);
                    if (m.from() != m.to())
                        limits.searchmoves.push_back(m);
                }
            }
            // "infinite" imposes no limit here.
        }
        return limits;
    }
}

void uci::loop()
{
    search::init();

    if (const char *path = std::getenv("KURGAN_NNUE"))
        nnue::load(path);
    else
        nnue::load(DEFAULT_NET);

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

    std::string line;
    while (std::getline(std::cin, line))
    {
        std::istringstream ss(line);
        std::string cmd;
        ss >> cmd;

        if (cmd == "uci")
        {
            std::cout << "id name Kurgan " << KURGAN_VERSION << std::endl;
            std::cout << "id author Hamza Inan" << std::endl;
            std::cout << "option name Hash type spin default 16 min 1 max 65536" << std::endl;
            std::cout << "option name Threads type spin default 1 min 1 max 256" << std::endl;
            std::cout << "option name Ponder type check default false" << std::endl;
            std::cout << "option name UCI_Chess960 type check default false" << std::endl;
            std::cout << "option name MultiPV type spin default 1 min 1 max 64" << std::endl;
            std::cout << "option name Clear Hash type button" << std::endl;
            std::cout << "option name EvalFile type string default " << DEFAULT_NET << std::endl;
            std::cout << "option name Use NNUE type check default true" << std::endl;
            std::cout << search::tuningOptionsUci();
            if (nnue::loaded())
                std::cout << "info string EvalFile " << nnue::file() << " id " << std::hex << nnue::hash()
                          << std::dec << std::endl;
            else
                std::cout << "info string evaluation " << evaluate::name() << " ("
                          << (nnue::supported() ? nnue::error() : "NNUE not compiled in") << ")" << std::endl;
            std::cout << "uciok" << std::endl;
        }
        else if (cmd == "isready")
        {
            std::cout << "readyok" << std::endl;
        }
        else if (cmd == "ucinewgame")
        {
            joinSearch();
            search::clear();
        }
        else if (cmd == "setoption")
        {
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

            try
            {
                if (name == "Hash")
                    search::setHashSize(std::stoi(value));
                else if (name == "Threads")
                    search::setThreads(std::stoi(value));
                else if (name == "Ponder")
                    search::setPonder(value == "true");
                else if (name == "UCI_Chess960")
                    movegen::setChess960(value == "true");
                else if (name == "MultiPV")
                    search::setMultiPV(std::stoi(value));
                else if (name == "Clear Hash")
                    search::clear();
                else if (name == "EvalFile")
                {
                    if (!value.empty() && !nnue::load(value))
                        std::cout << "info string NNUE load failed: " << nnue::error() << std::endl;
                }
                else if (name == "Use NNUE")
                    nnue::setEnabled(value == "true");
                else
                    search::setTuningOption(name, std::stoi(value));
            }
            catch (...)
            {
                // Ignore malformed option values.
            }
        }
        else if (cmd == "position")
        {
            joinSearch();
            parsePosition(ss, pos);
        }
        else if (cmd == "go")
        {
            joinSearch();
            const search::SearchLimits limits = parseGo(ss, pos);
            searchActive = true;
            searchThread = std::thread([pos, limits]()
                                       { search::go(pos, limits); });
        }
        else if (cmd == "stop")
        {
            search::stop();
            joinSearch();
        }
        else if (cmd == "ponderhit")
        {
            search::ponderhit();
        }
        else if (cmd == "perft")
        {
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
            joinSearch();
#ifdef KURGAN_HCE_OFF
            std::cout << "eval " << evaluate::evaluate(pos) << std::endl;
#else
            std::cout << "eval " << evaluate::hce(pos) << std::endl;
#endif
            if (nnue::loaded())
                std::cout << "eval nnue " << nnue::evaluate(pos) << std::endl;
        }
        else if (cmd == "debug")
        {
            joinSearch();
            pos.print_board();
            std::cout << "Fen: " << pos.fen() << std::endl;
        }
        else if (cmd == "bench")
        {
            joinSearch();
            int depth = 0;
            ss >> depth;
            bench::run(depth);
        }
        else if (cmd == "quit")
        {
            search::stop();
            break;
        }
    }

    joinSearch();
}