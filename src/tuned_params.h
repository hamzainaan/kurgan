#pragma once

#include <array>

namespace tuned
{

constexpr std::array<int, 6> MATERIAL = {
     183,  338,  349,  560,  910,    0,
};

constexpr std::array<std::array<int, 64>, 6> MG_PST = {
    {
           0,    0,    0,    0,    0,    0,    0,    0,
         221,  235,  199,  236,  206,  206,   69,   88,
         252,  258,  290,  257,  305,  359,  278,  232,
         264,  271,  256,  266,  287,  260,  279,  220,
         237,  230,  257,  274,  270,  268,  243,  218,
         226,  226,  250,  237,  262,  244,  273,  222,
         225,  241,  233,  236,  246,  271,  279,  230,
           0,    0,    0,    0,    0,    0,    0,    0,
        -232,  -78,  -91,  -55,    7, -138,  -57, -134,
         -82,  -65,   50,  -11,   43,   35,  -28,  -36,
         -24,    5,   20,   60,   64,  136,   13,   38,
          34,   45,   34,   43,   41,   59,   35,   41,
          28,   27,   53,   36,   55,   67,   39,   15,
          13,   18,   37,   34,   48,   50,   58,   13,
         -17,  -54,   -3,   14,   19,   22,   -1,   13,
        -101,  -27,  -55,  -32,  -20,  -15,  -32,  -47,
         -27,  -32, -105,  -92,  -65,  -79,   -4,  -12,
         -34,  -20,  -29,  -28,  -22,   61,  -32,  -26,
          -7,   27,   51,   19,   46,   44,   26,   13,
          11,   17,   31,   58,   50,   30,   31,   22,
          25,   36,   43,   62,   78,   41,   43,   19,
          51,   72,   58,   25,   47,   74,   62,   51,
          21,   67,   41,   32,   41,   69,   91,   36,
          14,   23,   26,   17,   30,    2,   -2,   24,
          23,   38,   15,   47,   51,   24,   32,   20,
          11,    4,   44,   66,   54,  101,   35,   41,
          11,   31,   25,   39,   43,   77,  129,   40,
          -7,   12,   29,   42,   23,   52,   38,   28,
         -18,  -13,  -11,   -1,   17,   17,   44,    2,
         -22,   -1,    2,   13,   18,   31,   33,    2,
         -32,  -18,  -22,   -4,    7,   13,   18,  -66,
          -2,   19,   30,   37,   46,   49,   -2,    5,
         -28,  -31,   -2,  -19,   31,   23,   11,   39,
         -48,  -74,  -55,  -60,  -98,   25,  -12,   41,
         -39,  -36,    9,  -26,   16,   -5,    6,  -16,
         -48,  -17,  -32,  -41,  -15,  -20,  -19,  -10,
          -8,  -20,  -10,  -21,    8,    5,    4,  -13,
         -18,   19,    5,    6,   11,   19,   25,    3,
          -7,   16,   24,   30,   36,   50,   42,   14,
           8,   16,   24,   32,   24,   -6,   -4,  -38,
         -47,  -15,   -2,  -35,  -46,  -42,  -21,  -22,
         -14,   18,   -9,    5,  -22,    6,  -19,  -45,
           0,   39,   61,   -1,   13,   66,   80,  -18,
         -13,    9,   25,  -29,  -27,    1,   18,  -83,
         -46,   36,  -12,  -73,  -77,  -49,  -38, -108,
         -24,  -16,  -27,  -63,  -77,  -52,  -14,  -80,
         -15,   -5,  -28,  -93,  -87,  -68,   10,  -10,
         -39,   28,  -11,  -89,  -79,  -81,   19,   -1,
    },
};

constexpr std::array<std::array<int, 64>, 6> EG_PST = {
    {
           0,    0,    0,    0,    0,    0,    0,    0,
          86,   99,  100,   70,   77,   65,  134,  106,
          21,   22,   18,   -4,  -16,  -27,    0,    7,
          -1,   -3,   -2,  -13,  -21,  -20,  -12,  -14,
         -11,   -4,  -20,  -18,  -18,  -28,  -19,  -25,
         -18,  -15,  -14,   -2,  -11,  -17,  -38,  -34,
         -10,  -14,    0,   -1,   -2,  -24,  -43,  -42,
           0,    0,    0,    0,    0,    0,    0,    0,
         -81,  -41,   12,  -10,   -1,  -14,  -48, -104,
         -13,   15,    9,   42,   25,   13,   -5,  -34,
          -1,   19,   64,   57,   39,   28,   13,  -19,
          20,   42,   64,   89,   72,   56,   26,    4,
          11,   31,   62,   67,   73,   48,   33,    1,
          -5,   28,   30,   62,   58,   24,    2,   -8,
         -19,    6,   22,   17,   12,   15,   -6,  -32,
         -32,  -19,   -1,    8,   -7,    3,  -26,  -60,
           9,   10,   23,   24,   22,   27,   14,    4,
          21,   29,   30,   13,   26,   18,   31,    9,
          26,   16,   20,   23,   25,   31,   26,   20,
          22,   26,   30,   27,   30,   16,   10,    6,
           9,   10,   28,   24,   21,   35,    9,    1,
           0,   12,   21,   32,   30,    7,    0,  -12,
           6,  -13,    0,    9,   10,    4,  -13,  -25,
         -10,    9,   -8,   11,    9,    9,    9,  -10,
         117,  108,  118,  105,  106,  113,  115,  109,
          96,  109,   97,   89,   82,   88,  105,   92,
         112,  101,   98,   93,   96,   87,   76,   92,
         110,  101,  105,   90,   94,   95,   94,   99,
         114,  117,  115,  111,  105,  103,   93,   96,
         109,  108,  104,  106,   97,   89,   90,   87,
          79,   79,   94,   79,   69,   68,   65,   82,
         113,  101,   99,   98,   84,   86,   97,   79,
          12,   58,   33,   50,   36,   48,   29,   40,
          32,   53,   53,   75,   93,   62,   73,   24,
          16,   22,   10,   57,   61,   41,   56,   52,
          49,   34,   29,   56,   41,   48,   64,   40,
         -11,   34,    8,   41,   19,   23,   36,   36,
          -4,  -37,    8,  -15,   -3,   10,   22,    9,
         -12,  -28,  -39,  -26,  -33,  -58,  -70,  -26,
         -31,  -54,  -38,  -56,  -24,  -30,  -24,  -23,
        -102,  -25,  -16,   -9,   -4,    8,    2,  -45,
         -19,   15,   14,   15,   10,   25,   23,    0,
           1,    5,   -5,   -2,   -5,   11,   14,    4,
         -23,   -6,   -6,   -3,  -10,   -1,   -7,    1,
         -19,  -19,   -2,   -1,   -1,   -2,   -5,   -2,
         -20,   -4,   -1,   -1,   -1,    0,   -6,   -3,
         -30,  -13,   -4,    5,    1,    2,  -12,  -23,
         -64,  -51,  -30,  -22,  -32,  -19,  -52,  -75,
    },
};

constexpr std::array<int, 4> MG_MOBILITY = {
       4,    7,    6,    2,
};
constexpr std::array<int, 4> EG_MOBILITY = {
      -9,    0,    1,    9,
};

constexpr std::array<int, 2> MG_OUTPOST = {
      40,   49,
};
constexpr std::array<int, 2> EG_OUTPOST = {
      23,   19,
};

constexpr std::array<int, 11> MG_MINOR = {
      34,  -18,   -4,   -2,  -10,  -16,  -25,  -29,  -38,  -63,  -24,
};
constexpr std::array<int, 11> EG_MINOR = {
      78,   13,    2,  -10,  -13,  -15,  -11,  -10,  -33,  -48,   -9,
};
constexpr std::array<int, 8> MG_ROOK = {
      56,   40,    0,   14,   20,   23,   -3,   -2,
};
constexpr std::array<int, 8> EG_ROOK = {
      -2,  -16,   26,   24,   36,   65,   11,  -51,
};

constexpr int MG_ISOLATED = 6;
constexpr int EG_ISOLATED = 8;
constexpr int MG_DOUBLED = 1;
constexpr int EG_DOUBLED = 12;

constexpr std::array<int, 8> MG_PASSED = {
       0,   24,   25,   15,   40,   69,  189,    0,
};
constexpr std::array<int, 8> EG_PASSED = {
       0,  -17,  -27,   -4,   32,  110,  129,    0,
};

constexpr std::array<int, 16> MG_PAWN_STRUCT = {
     -17,   24,   15,  -12,   25,   25,   -1,   -9,
       4,   -1,   -2,    8,  -13,    4,   17,  -19,
};
constexpr std::array<int, 16> EG_PAWN_STRUCT = {
       0,   11,   -8,   -2,   17,   26,  -13,    7,
     -13,    4,    5,   13,   21,   -2,  -32,   46,
};

constexpr std::array<int, 22> MG_THREATS = {
     -16,   -5,   36,   87,   30,  -15,   20,    6,   61,   44,  -29,
       1,    9,    6,   50,  -90,   77,   20,    4,   18,   26,   41,
};
constexpr std::array<int, 22> EG_THREATS = {
      19,    3,   52,   27,   -1,   12,   49,   21,   31,   39,   21,
      30,   38,   10,   18,   64,   39,   19,   12,  -12,   21,    3,
};

constexpr std::array<std::array<int, 5>, 5> MG_IMBALANCE = {
    {
        {   0,    0,    0,    0,    0},
        {  26,    0,    0,    0,    0},
        {  24,    3,    0,    0,    0},
        {  28,  -32,  -44,    0,    0},
        { 123,  -39,  -28, -148,    0},
    },
};
constexpr std::array<std::array<int, 5>, 5> EG_IMBALANCE = {
    {
        {   0,    0,    0,    0,    0},
        {  47,    0,    0,    0,    0},
        {  48,  -24,    0,    0,    0},
        {  78,  -17,   -9,    0,    0},
        { 145,   67,   86,  242,    0},
    },
};

constexpr int TEMPO = 46;

// --- KING SAFETY BLOCK (preserved verbatim by kurgan_tune.py) ---
constexpr std::array<std::array<int, 8>, 4> PAWN_SHIELD_VALUE = {
    {
            {  -17,   12,   16,   -3,   19,   33,    4,    0 },
            {  -23,   49,   38,   -2,    1,   24,   -1,    0 },
            {  -23,   44,   10,    5,   12,   13,   19,    0 },
            {  -11,   21,   24,   16,    7,   -4,   10,    0 },
    },
};
constexpr std::array<std::array<std::array<int, 8>, 4>, 3> PAWN_STORM_VALUE = {
    {
        {
            {
                {   13,  -52,  -15,   20,   44,   19,   12,    0 },
                {   17,  -83,  -23,    1,    4,  -10,    6,    0 },
                {   19,  -32,   13,   -2,   10,    4,   -3,    0 },
                {    9,  -35,   -2,   10,    1,  -20,   -9,    0 },
            },
        },
        {
            {
                {    6,    3,   41,    0,    7,   11,   26,    0 },
                {   10,    5,   67,   -8,   17,    5,   -9,    0 },
                {   14,    7,   71,   12,   18,    7,   36,    0 },
                {    8,    4,   53,   11,   -7,  -12,  -20,    0 },
            },
        },
        {
            {
                {    6,   16,   21,   31,   15,   10,    3,    0 },
                {   10,   33,   12,    9,   24,    8,   15,    0 },
                {   14,   10,   33,   34,   18,   24,    6,    0 },
                {    8,   -1,   -8,   23,    4,   -8,   -1,    0 },
            },
        },
    },
};
constexpr int PAWN_STORM_SHIELDING_KING = -148;
constexpr std::array<int, 3> CASTLING_RIGHTS_VALUE = {-1, 23, 64};
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
constexpr int KS_ATTACK_SCALE = 69;
constexpr int KS_SCALE = 105;
// --- END KING SAFETY BLOCK ---

inline int RFP_DEPTH = 7;
inline int RFP_MARGIN = 81;
inline int RAZOR_DEPTH = 3;
inline int RAZOR_MARGIN = 250;
inline int NMP_MIN_DEPTH = 3;
inline int NMP_BASE = 3;
inline int NMP_DEPTH_DIV = 4;
inline int NMP_EVAL_DIV = 200;
inline int NMP_VERIFY_DEPTH = 12;
inline int PROBCUT_DEPTH = 5;
inline int PROBCUT_MARGIN = 151;
inline int IIR_DEPTH = 4;
inline int LMP_DEPTH = 8;
inline int LMP_BASE = 3;
inline int HIST_PRUNE_DEPTH = 4;
inline int HIST_PRUNE_MARGIN = 2470;
inline int FUTILITY_DEPTH = 8;
inline int FUTILITY_BASE = 101;
inline int FUTILITY_MARGIN = 100;
inline int SEE_QUIET_MARGIN = 25;
inline int SEE_CAPTURE_MARGIN = 91;
inline int CAPTURE_FUTILITY_DEPTH = 6;
inline int CAPTURE_FUTILITY_BASE = 200;
inline int CAPTURE_FUTILITY_MARGIN = 202;
inline int SEE_ORDER_DIV = 72;
inline int QS_FUTILITY_MARGIN = 200;
inline int SE_DEPTH = 7;
inline int SE_MARGIN = 12;
inline int SE_DOUBLE_MARGIN = 20;
inline int LMR_BASE = 75;
inline int LMR_DIVISOR = 226;
inline int LMR_HIST_DIV = 6017;
inline int LMR_CAPTURE_BASE = 2;
inline int LMR_CAPHIST_DIV = 5055;
inline int HIST_BONUS_MUL = 32;
inline int HIST_BONUS_MAX = 4098;

} // namespace tuned
