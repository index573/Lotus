#include "Consoles.h"

#include <cmath>

namespace lotus
{

const ConsoleProfile& getConsoleProfile (ConsoleId id)
{
    // Note on rates:
    //  - SNES S-DSP runs at 32000 Hz; pitch is a 14-bit 4.12 value (max 0x3FFF, ~2 octaves up).
    //  - DS mixer: 33.513982 MHz / 1024 = ~32728.5 Hz, 10-bit output, no interpolation.
    //    Channel rates come from a 16-bit timer: rate = 16756991 / timer.
    //  - GBA: there is no hardware sample engine; most games used Nintendo's
    //    "m4a"/MusicPlayer2000 software mixer. 13379 Hz is its most common mixing
    //    rate; samples are signed 8-bit and the DirectSound FIFO is 8-bit.
    //  - PS1 SPU runs at 44100 Hz with 4.12 pitch (steps above 0x3FFF clip to 0x4000).
    static const ConsoleProfile profiles[] =
    {
        { "Modern",      "Clean 4-point Hermite interpolation at the host sample rate.",
          0.0,      Interp::Hermite,   PitchMode::Free,      0,      0,  0 },
        { "Linear",      "Linear interpolation at the host sample rate (typical of many software samplers).",
          0.0,      Interp::Linear,    PitchMode::Free,      0,      0,  0 },
        { "Raw",         "No interpolation at all, at the host sample rate. Crunchy and aliased.",
          0.0,      Interp::Nearest,   PitchMode::Free,      0,      0,  0 },
        { "SNES",        "S-DSP: 32 kHz mixer, 4-tap Gaussian interpolation, 4.12 fixed-point pitch.",
          32000.0,  Interp::SnesGauss, PitchMode::Fixed4p12, 0x3FFF, 16, 16 },
        { "Nintendo DS", "DS mixer: ~32.73 kHz, no interpolation, timer-quantised pitch, 10-bit output.",
          33513982.0 / 1024.0, Interp::Nearest, PitchMode::DsTimer, 0, 16, 10 },
        { "GBA",         "m4a-style software mixer: 13.38 kHz, 8-bit samples and output, linear interpolation.",
          13379.0,  Interp::Linear,    PitchMode::Free,      0,      8,  8 },
        { "PS1",         "SPU: 44.1 kHz mixer, 4-tap Gaussian interpolation, 4.12 fixed-point pitch.",
          44100.0,  Interp::Ps1Gauss,  PitchMode::Fixed4p12, 0x4000, 16, 16 },
    };

    static_assert (sizeof (profiles) / sizeof (profiles[0]) == (size_t) ConsoleId::count);

    const int i = (int) id;
    return profiles[(i >= 0 && i < numConsoles) ? i : 0];
}

const std::array<int16_t, 512>& snesGaussTable()
{
    // Construction of the S-DSP Gaussian table, from ares (ISC licence,
    // Copyright (c) 2004-2025 ares team, Near et al). It reproduces the 512-entry
    // table from the SNES hardware. See THIRD_PARTY_NOTICES.md.
    static const std::array<int16_t, 512> table = []
    {
        std::array<int16_t, 512> out {};
        double t[512];
        const double pi = 3.14159265358979323846;
        for (int n = 0; n < 512; ++n)
        {
            const double k = 0.5 + n;
            const double s = std::sin (pi * k * 1.280 / 1024);
            const double tt = (std::cos (pi * k * 2.000 / 1023) - 1) * 0.50;
            const double u = (std::cos (pi * k * 4.000 / 1023) - 1) * 0.08;
            t[511 - n] = s * (tt + u + 1.0) / k;
        }
        for (int phase = 0; phase < 128; ++phase)
        {
            const double sum = t[phase] + t[phase + 256] + t[511 - phase] + t[255 - phase];
            const double scale = 2048.0 / sum;
            out[(size_t) phase]         = (int16_t) (t[phase] * scale + 0.5);
            out[(size_t) phase + 256]   = (int16_t) (t[phase + 256] * scale + 0.5);
            out[(size_t) (511 - phase)] = (int16_t) (t[511 - phase] * scale + 0.5);
            out[(size_t) (255 - phase)] = (int16_t) (t[255 - phase] * scale + 0.5);
        }
        return out;
    }();
    return table;
}

const std::array<int16_t, 512>& ps1GaussTable()
{
    // PS1 SPU Gaussian interpolation table, as documented in psx-spx
    // ("Sound Processing Unit (SPU)", 4-Point Gaussian Interpolation).
    static const std::array<int16_t, 512> table =
    {
    -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1,
    0, 0, 0, 0, 0, 0, 0, 1,
    1, 1, 1, 2, 2, 2, 3, 3,
    3, 4, 4, 5, 5, 6, 7, 7,
    8, 9, 9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 21, 22, 24,
    25, 27, 28, 30, 32, 33, 35, 37,
    39, 41, 44, 46, 48, 51, 53, 56,
    58, 61, 64, 67, 70, 73, 77, 80,
    84, 87, 91, 95, 99, 103, 107, 111,
    116, 120, 125, 130, 135, 140, 145, 150,
    156, 161, 167, 173, 179, 186, 192, 199,
    205, 212, 219, 227, 234, 242, 250, 257,
    266, 274, 283, 291, 300, 309, 319, 328,
    338, 348, 358, 369, 379, 390, 401, 412,
    424, 436, 448, 460, 473, 485, 498, 512,
    525, 539, 553, 567, 582, 597, 612, 627,
    643, 659, 675, 692, 708, 726, 743, 761,
    779, 797, 816, 835, 854, 874, 894, 914,
    935, 956, 977, 999, 1020, 1043, 1066, 1089,
    1112, 1136, 1160, 1184, 1209, 1234, 1260, 1286,
    1312, 1339, 1366, 1394, 1422, 1450, 1479, 1508,
    1537, 1567, 1598, 1628, 1660, 1691, 1723, 1756,
    1789, 1822, 1856, 1890, 1924, 1959, 1995, 2031,
    2067, 2104, 2141, 2179, 2217, 2256, 2295, 2334,
    2374, 2415, 2456, 2497, 2539, 2582, 2624, 2668,
    2712, 2756, 2801, 2846, 2892, 2938, 2985, 3032,
    3079, 3128, 3176, 3225, 3275, 3325, 3376, 3427,
    3479, 3531, 3584, 3637, 3691, 3745, 3799, 3855,
    3910, 3967, 4023, 4081, 4138, 4197, 4255, 4315,
    4374, 4435, 4495, 4557, 4619, 4681, 4744, 4807,
    4871, 4935, 5000, 5065, 5131, 5197, 5264, 5332,
    5399, 5468, 5536, 5606, 5676, 5746, 5817, 5888,
    5959, 6032, 6104, 6177, 6251, 6325, 6400, 6475,
    6550, 6626, 6702, 6779, 6856, 6934, 7012, 7091,
    7170, 7249, 7329, 7409, 7490, 7571, 7653, 7735,
    7817, 7900, 7983, 8066, 8150, 8234, 8319, 8404,
    8489, 8575, 8661, 8748, 8834, 8922, 9009, 9097,
    9185, 9273, 9362, 9451, 9541, 9630, 9720, 9811,
    9901, 9992, 10083, 10174, 10266, 10358, 10450, 10542,
    10635, 10727, 10820, 10913, 11007, 11100, 11194, 11288,
    11382, 11476, 11571, 11665, 11760, 11855, 11950, 12045,
    12140, 12236, 12331, 12427, 12522, 12618, 12714, 12809,
    12905, 13001, 13097, 13193, 13289, 13385, 13481, 13577,
    13673, 13769, 13865, 13961, 14056, 14152, 14248, 14343,
    14439, 14534, 14630, 14725, 14820, 14915, 15010, 15104,
    15199, 15293, 15387, 15481, 15575, 15669, 15762, 15855,
    15948, 16041, 16133, 16226, 16317, 16409, 16500, 16592,
    16682, 16773, 16863, 16953, 17042, 17131, 17220, 17308,
    17396, 17484, 17571, 17658, 17744, 17830, 17916, 18001,
    18086, 18170, 18254, 18337, 18420, 18502, 18584, 18665,
    18746, 18826, 18905, 18985, 19063, 19141, 19219, 19295,
    19372, 19447, 19522, 19597, 19671, 19744, 19816, 19888,
    19959, 20030, 20100, 20169, 20238, 20306, 20373, 20439,
    20505, 20570, 20634, 20698, 20760, 20822, 20884, 20944,
    21004, 21063, 21121, 21178, 21235, 21290, 21345, 21399,
    21452, 21505, 21556, 21607, 21657, 21706, 21754, 21801,
    21848, 21893, 21938, 21982, 22025, 22066, 22107, 22148,
    22187, 22225, 22262, 22299, 22334, 22369, 22402, 22435,
    22467, 22498, 22527, 22556, 22584, 22611, 22637, 22662,
    22686, 22709, 22731, 22752, 22772, 22791, 22809, 22826,
    22842, 22857, 22872, 22885, 22897, 22908, 22918, 22927,
    22935, 22942, 22948, 22953, 22957, 22960, 22962, 22963,
    };
    return table;
}

} // namespace lotus
