#pragma once

#include <array>

namespace tuned
{

constexpr std::array<int, 6> MATERIAL = {
     180,  337,  348,  558,  909,    0,
};

constexpr std::array<std::array<int, 64>, 6> MG_PST = {
    {
           0,    0,    0,    0,    0,    0,    0,    0,
         217,  250,  205,  239,  203,  203,   49,   81,
         235,  229,  270,  256,  293,  354,  282,  228,
         225,  252,  239,  262,  267,  247,  252,  206,
         207,  205,  241,  246,  253,  233,  216,  195,
         199,  204,  218,  200,  222,  205,  237,  195,
         194,  218,  201,  215,  228,  259,  266,  207,
           0,    0,    0,    0,    0,    0,    0,    0,
        -207,  -72,  -87,  -50,    5, -118,  -54, -128,
         -69,  -64,   61,    1,   24,   48,  -25,  -27,
         -27,   27,   26,   48,   72,  119,   17,   35,
          15,   34,   30,   29,   46,   72,   41,   29,
          16,   26,   50,   34,   54,   62,   42,   27,
           3,    4,   32,   30,   47,   48,   50,   10,
         -24,  -69,  -19,    6,   17,   18,  -12,   -4,
         -99,  -29,  -64,  -50,   -7,  -27,  -28,  -48,
         -30,  -28, -108,  -86,  -54,  -75,   -6,  -12,
         -30,   -9,  -30,  -28,   -8,   61,   -7,  -60,
         -17,   32,   30,   25,   22,   40,   20,   -7,
          22,   24,   20,   67,   50,   46,   22,   18,
          21,   41,   46,   56,   74,   45,   38,   31,
          29,   59,   59,   31,   45,   79,   61,   36,
          23,   63,   46,   32,   42,   59,   87,   28,
         -12,   28,   20,   12,   33,   11,   -7,    6,
          22,   34,   10,   47,   48,   20,   30,   25,
          11,   19,   49,   60,   56,   98,   18,   34,
          -2,   31,   28,   31,   30,   74,  125,   33,
         -14,    3,   23,   41,   24,   53,   26,    6,
         -28,  -12,    2,   -1,   23,   13,   44,   -2,
         -23,    3,    9,   11,   14,   35,   31,   -4,
         -46,  -14,  -24,    5,   19,   21,   14,  -64,
           1,   16,   24,   37,   41,   49,    0,    7,
         -27,  -29,   -3,  -26,   26,   23,   16,   49,
         -50,  -89,  -36,  -43,  -86,   12,  -18,   23,
         -33,  -35,    1,  -34,    2,   -6,  -10,   -5,
         -48,  -26,  -33,  -50,  -13,  -18,   -7,  -11,
           8,  -35,    5,  -10,    8,    6,   10,  -12,
         -15,   25,    8,   12,   16,   18,   28,   12,
         -21,    5,   32,   29,   43,   57,   33,   17,
           8,   14,   24,   37,   22,  -10,    1,  -46,
         -49,  -18,   -6,  -36,  -47,  -43,  -22,  -24,
         -16,   13,  -12,    4,  -20,    5,  -18,  -44,
          -2,   32,   59,    1,   14,   62,   74,  -19,
         -11,    8,   28,  -22,  -20,    8,   19,  -82,
         -44,   30,   -8,  -63,  -67,  -39,  -38, -103,
         -29,  -19,  -22,  -51,  -73,  -47,  -21,  -87,
         -31,   -5,  -26,  -96,  -82,  -51,  -12,  -17,
         -58,   28,    3,  -97,  -51,  -78,    6,  -12,
    },
};

constexpr std::array<std::array<int, 64>, 6> EG_PST = {
    {
           0,    0,    0,    0,    0,    0,    0,    0,
         147,  141,  129,   91,  108,   90,  156,  158,
          46,   51,   26,   -8,  -23,  -14,   20,   35,
          10,    0,   -3,  -19,  -14,  -11,   -1,    8,
          -4,   -4,  -19,  -13,  -12,  -19,  -14,   -9,
         -20,  -17,  -18,    3,   -2,  -11,  -34,  -23,
         -15,  -24,    1,   -9,    3,  -19,  -34,  -28,
           0,    0,    0,    0,    0,    0,    0,    0,
         -83,  -37,   15,  -13,    0,   -9,  -49, -103,
         -14,   18,    0,   48,   23,   12,    3,  -36,
          -4,   22,   59,   52,   35,   19,   12,  -26,
          22,   40,   64,   83,   68,   48,   26,    7,
          15,   27,   60,   66,   63,   54,   43,    2,
           3,   37,   36,   60,   51,   34,    5,    2,
         -20,    4,   23,   21,   24,    8,  -10,  -31,
         -31,  -39,   -5,   11,   -8,    0,  -46,  -64,
           7,   11,   17,   20,   28,   27,    8,    1,
          24,   26,   31,   13,   32,   24,   32,    8,
          25,   11,   19,   14,   22,   24,   22,   27,
          15,   20,   32,   24,   31,   22,   10,   12,
          13,   13,   29,   38,   18,   28,    6,    9,
          11,   15,   29,   28,   35,    9,   17,  -11,
           5,   -9,    2,   12,    9,    6,  -19,  -33,
         -11,   12,    5,   13,   11,    9,   15,  -11,
         114,  106,  120,  114,  113,  116,  106,  111,
          97,   97,   89,   81,   77,   80,  105,   92,
         107,   98,   96,   94,   92,   82,   72,   87,
         110,  102,  110,   87,   97,  100,   92,  106,
         120,  121,  118,  106,   98,   99,   95,   99,
         108,  111,  100,   98,   93,   85,   96,   85,
          86,   78,   88,   81,   64,   65,   57,   78,
         105,  102,  104,   92,   85,   84,   97,   77,
          -2,   54,   31,   39,   36,   45,   33,   39,
          22,   51,   62,   74,   97,   44,   68,   28,
          20,   25,   -1,   58,   53,   47,   54,   58,
          53,   35,   32,   58,   41,   47,   74,   42,
         -21,   34,   11,   41,   18,   28,   35,   41,
          -3,  -39,    4,  -17,   -5,   12,   19,   13,
         -16,  -35,  -49,  -32,  -35,  -54,  -71,  -29,
         -31,  -61,  -38,  -43,  -21,  -30,  -25,  -28,
        -117,  -36,  -26,  -16,  -10,    2,    4,  -51,
         -28,   21,   26,   32,   29,   44,   33,   -7,
           8,   14,   16,   18,   16,   48,   39,   -2,
         -21,   14,   17,   26,   19,   28,   12,   -5,
         -32,  -26,   15,   26,   31,   21,   -4,  -24,
         -41,  -13,    2,   13,   20,   13,   -4,  -23,
         -65,  -34,  -10,    4,    5,   -7,  -29,  -55,
        -108,  -90,  -60,  -36,  -59,  -34,  -75, -113,
    },
};

constexpr std::array<int, 4> MG_MOBILITY = {
       3,    6,    7,    1,
};
constexpr std::array<int, 4> EG_MOBILITY = {
      -4,    3,    3,   11,
};

constexpr std::array<int, 2> MG_OUTPOST = {
      43,   48,
};
constexpr std::array<int, 2> EG_OUTPOST = {
      24,   19,
};

constexpr std::array<int, 11> MG_MINOR = {
      34,  -26,   -9,   -1,   -7,  -15,  -23,  -35,  -40,  -64,  -27,
};
constexpr std::array<int, 11> EG_MINOR = {
      79,   24,    6,   -9,  -13,  -15,  -13,  -15,  -35,  -48,   -8,
};
constexpr std::array<int, 8> MG_ROOK = {
      68,   42,    5,   16,   23,   23,    3,   -6,
};
constexpr std::array<int, 8> EG_ROOK = {
      -3,   -9,   29,   22,   33,   57,   20,  -55,
};

constexpr int MG_ISOLATED = 7;
constexpr int EG_ISOLATED = 10;
constexpr int MG_DOUBLED = 3;
constexpr int EG_DOUBLED = 12;

constexpr std::array<int, 8> MG_PASSED = {
       0,   17,   20,   17,   41,   74,  188,    0,
};
constexpr std::array<int, 8> EG_PASSED = {
       0,  -46,  -44,  -13,   31,  122,  152,    0,
};

constexpr std::array<int, 8> MG_PAWN_STRUCT = {
     -13,   26,   13,   -4,   23,   24,    8,   -8,
};
constexpr std::array<int, 8> EG_PAWN_STRUCT = {
      -1,   12,   -1,   -6,    1,   22,  -23,   20,
};

constexpr std::array<int, 22> MG_THREATS = {
     -13,   -5,   47,   89,   38,  -13,   29,    6,   72,   53,  -29,
      10,   17,    6,   59,  -57,  102,   24,    6,   26,   35,   50,
};
constexpr std::array<int, 22> EG_THREATS = {
      20,    3,   43,   21,   -2,    6,   54,   21,   34,   41,   27,
      30,   46,   10,   22,   85,   49,   15,   13,  -13,   23,    7,
};

constexpr std::array<std::array<int, 5>, 5> MG_IMBALANCE = {
    {
        {   0,    0,    0,    0,    0},
        {  28,    0,    0,    0,    0},
        {  22,    5,    0,    0,    0},
        {  22,  -29,  -40,    0,    0},
        { 110,  -26,  -18, -124,    0},
    },
};
constexpr std::array<std::array<int, 5>, 5> EG_IMBALANCE = {
    {
        {   0,    0,    0,    0,    0},
        {  47,    0,    0,    0,    0},
        {  49,  -20,    0,    0,    0},
        {  78,   -9,   -6,    0,    0},
        { 145,   64,   81,  241,    0},
    },
};

constexpr int TEMPO = 41;

// --- KING SAFETY BLOCK (preserved verbatim by kurgan_tune.py) ---
constexpr std::array<std::array<int, 8>, 4> PAWN_SHIELD_VALUE = {
    {
        -12,  22,  26,  11,   8,   5, -11,   0,
        -18,  38,  24,  -1,  -2,  -5, -17,   0,
        -13,  38,   5,  -3,  -4,  -5,  -7,   0,
         -8,  15,   8,   6,  -1,  -6,  -8,   0,
    },
};
constexpr std::array<std::array<std::array<int, 8>, 4>, 3> PAWN_STORM_VALUE = {
    {
         6,  10,  14,  18,  22,  26,   0,   0,
        10,  14,  18,  24,  30,  36,   0,   0,
        14,  18,  24,  30,  38,  46,   0,   0,
         8,  12,  16,  20,  24,  28,   0,   0,
         6,   3,   5,   7,   9,  11,   0,   0,
        10,   5,   8,  12,  16,  20,   0,   0,
        14,   7,  12,  18,  24,  30,   0,   0,
         8,   4,   6,   9,  12,  15,   0,   0,
         6,  12,  20,  26,  30,  34,   0,   0,
        10,  16,  26,  34,  40,  46,   0,   0,
        14,  20,  32,  42,  50,  58,   0,   0,
         8,  14,  22,  30,  36,  42,   0,   0,
    },
};
constexpr int PAWN_STORM_SHIELDING_KING = -148;
constexpr std::array<int, 3> CASTLING_RIGHTS_VALUE = {0, 25, 61};
constexpr int KS_BASE = -18;
constexpr std::array<int, 4> KING_THREAT_MULTIPLIER = {7, 4, 5, 5};
constexpr std::array<int, 4> KING_THREAT_SQUARE = {8, 13, 11, 13};
constexpr int KING_DEFENSELESS_SQUARE = 22;
constexpr int KS_PAWN_FACTOR = 10;
constexpr int KING_PRESSURE = 2;
constexpr int KS_KING_PRESSURE_FACTOR = 10;
constexpr std::array<int, 4> SAFE_CHECK_BONUS = {78, 27, 47, 51};
constexpr int KS_NO_KNIGHT_DEFENDER = 15;
constexpr int KS_NO_BISHOP_DEFENDER = 15;
constexpr int KS_BISHOP_PRESSURE = 8;
constexpr int KS_NO_QUEEN = -40;
constexpr int KS_ARRAY_FACTOR = 128;
constexpr int KS_MAX = 600;
constexpr int KS_SCALE = 82;
// --- END KING SAFETY BLOCK ---

inline int RFP_DEPTH = 7;
inline int RFP_MARGIN = 80;
inline int FUTILITY_DEPTH = 0;
inline int FUTILITY_MARGIN = 120;
inline int MOVE_FUTILITY_DEPTH = 4;
inline int MOVE_FUTILITY_MARGIN = 90;
inline int SEE_QUIET_DEPTH = 6;
inline int NMP_MIN_DEPTH = 3;
inline int NMP_BASE = 3;
inline int NMP_DEPTH_DIV = 4;
inline int NMP_EVAL_DIV = 200;
inline int NMP_VERIFY_DEPTH = 12;
inline int RAZOR_DEPTH = 3;
inline int RAZOR_MARGIN = 200;
inline int PROBCUT_DEPTH = 9;
inline int PROBCUT_MARGIN = 80;
inline int IID_DEPTH = 5;
inline int LMP_DEPTH = 10;

} // namespace tuned
