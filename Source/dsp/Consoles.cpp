#include "Consoles.h"

#include <cmath>

namespace lotus
{

const int nesDmcPeriods[16] = { 428, 380, 340, 320, 286, 254, 226, 214, 190, 160, 142, 128, 106, 84, 72, 54 };

const ConsoleProfile& getConsoleProfile (ConsoleId id)
{
    static const ConsoleProfile profiles[] =
    {
        { "Modern", "Clean 4-point Hermite interpolation at the project rate.", "Clean",
          0.0, Interp::Hermite, PitchMode::Free, 0, 0, 0, Codec::None, 0, false, 0.0f, { 0.0f, 0.0f }, false },
        { "Linear", "Linear interpolation at the project rate.", "Clean",
          0.0, Interp::Linear, PitchMode::Free, 0, 0, 0, Codec::None, 0, false, 0.0f, { 0.0f, 0.0f }, false },
        { "Raw", "No interpolation at the project rate. Crunchy and aliased.", "Clean",
          0.0, Interp::Nearest, PitchMode::Free, 0, 0, 0, Codec::None, 0, false, 0.0f, { 0.0f, 0.0f }, false },

        { "NES", "DMC: 16 fixed rates, so notes snap and detune. 7-bit output, APU filters.", "Nintendo",
          0.0, Interp::Nearest, PitchMode::NesDmc, 0, 0, 7, Codec::Dpcm, 1, false, 14000.0f, { 90.0f, 440.0f }, false },
        { "SNES", "S-DSP: 32 kHz, 4-tap Gaussian interpolation, 4.12 pitch.", "Nintendo",
          32000.0, Interp::SnesGauss, PitchMode::Fixed4p12, 0x3FFF, 16, 16, Codec::Brr, 8, false, 0.0f, { 0.0f, 0.0f }, false },
        { "N64", "RSP: 32 kHz, 4-tap polyphase resampler, one octave of headroom.", "Nintendo",
          32000.0, Interp::N64Poly, PitchMode::N64Q15, 0, 16, 16, Codec::None, 0, false, 0.0f, { 0.0f, 0.0f }, false },
        { "GBA", "m4a-style mixer: 13.38 kHz, linear, 8-bit output, held DAC.", "Nintendo",
          13379.0, Interp::Linear, PitchMode::Free, 0, 8, 8, Codec::None, 12, true, 0.0f, { 0.0f, 0.0f }, false },
        { "Nintendo DS", "32.73 kHz, no interpolation, timer pitch, 10-bit output.", "Nintendo",
          33513982.0 / 1024.0, Interp::Nearest, PitchMode::DsTimer, 0, 16, 10, Codec::ImaAdpcm, 16, true, 0.0f, { 0.0f, 0.0f }, false },

        { "PS1", "SPU: 44.1 kHz, 4-tap Gaussian interpolation, 4.12 pitch.", "Sony",
          44100.0, Interp::Ps1Gauss, PitchMode::Fixed4p12, 0x4000, 16, 16, Codec::PsAdpcm, 24, false, 0.0f, { 0.0f, 0.0f }, false },
        { "PS2", "SPU2: 48 kHz, 4-tap Gaussian interpolation, 4.12 pitch.", "Sony",
          48000.0, Interp::Ps1Gauss, PitchMode::Fixed4p12, 0x4000, 16, 16, Codec::PsAdpcm, 48, false, 0.0f, { 0.0f, 0.0f }, false },

        { "Mega Drive", "YM2612 DAC, 14 kHz Z80 mixer, ladder distortion, model 1 filter.", "Sega",
          14000.0, Interp::Nearest, PitchMode::Free, 0, 8, 8, Codec::None, 4, true, 2842.0f, { 0.0f, 0.0f }, true },
        { "Saturn / Dreamcast", "SCSP/AICA: 44.1 kHz, linear, octave + 10-bit F-number pitch.", "Sega",
          44100.0, Interp::Linear, PitchMode::ScspOctFns, 0, 16, 16, Codec::None, 32, false, 0.0f, { 0.0f, 0.0f }, false },

        { "Amiga 500", "Paula: period pitch, no interpolation, 4.4 kHz RC filter.", "Commodore",
          amigaPalClock / 32.0, Interp::Nearest, PitchMode::AmigaPeriod, 0, 8, 0, Codec::None, 4, false, 4420.97f, { 5.128f, 0.0f }, false },
        { "Amiga 1200", "Paula: period pitch, no interpolation, open top end.", "Commodore",
          amigaPalClock / 32.0, Interp::Nearest, PitchMode::AmigaPeriod, 0, 8, 0, Codec::None, 4, false, 0.0f, { 5.319f, 0.0f }, false },
    };

    static_assert (sizeof (profiles) / sizeof (profiles[0]) == (size_t) ConsoleId::count);

    const int i = (int) id;
    return profiles[(i >= 0 && i < numConsoles) ? i : 0];
}

const char* codecName (Codec c)
{
    switch (c)
    {
        case Codec::Brr:      return "BRR";
        case Codec::PsAdpcm:  return "ADPCM";
        case Codec::ImaAdpcm: return "IMA-ADPCM";
        case Codec::Dpcm:     return "DPCM";
        case Codec::None:     break;
    }
    return "PCM";
}

const std::array<int16_t, 512>& snesGaussTable()
{
    // Table construction from ares (ISC licence); see THIRD_PARTY_NOTICES.md.
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
    // From psx-spx, "4-Point Gaussian Interpolation".
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

const std::array<int16_t, 256>& n64ResampleTable()
{
    // RSP audio microcode resampler coefficients: 64 phases x 4 taps, Q15.
    // Values as used by mupen64plus-rsp-hle (RESAMPLE_LUT).
    static const std::array<int16_t, 256> table =
    {
        3129, 26285, 3398, -33,
        2873, 26262, 3679, -40,
        2628, 26217, 3971, -48,
        2394, 26150, 4276, -56,
        2173, 26061, 4592, -65,
        1963, 25950, 4920, -74,
        1764, 25817, 5260, -84,
        1576, 25663, 5611, -95,
        1399, 25487, 5974, -106,
        1233, 25291, 6347, -118,
        1077, 25075, 6732, -130,
        932, 24838, 7127, -143,
        796, 24583, 7532, -156,
        671, 24309, 7947, -170,
        554, 24016, 8371, -184,
        446, 23706, 8804, -198,
        347, 23379, 9246, -212,
        257, 23036, 9696, -226,
        174, 22678, 10153, -240,
        99, 22304, 10618, -254,
        31, 21917, 11088, -268,
        -30, 21517, 11564, -280,
        -84, 21104, 12045, -293,
        -132, 20679, 12531, -304,
        -173, 20244, 13020, -314,
        -210, 19799, 13512, -323,
        -241, 19345, 14006, -330,
        -267, 18882, 14501, -336,
        -289, 18413, 14997, -340,
        -306, 17937, 15493, -341,
        -320, 17456, 15988, -340,
        -330, 16970, 16480, -337,
        -337, 16480, 16970, -330,
        -340, 15988, 17456, -320,
        -341, 15493, 17937, -306,
        -340, 14997, 18413, -289,
        -336, 14501, 18882, -267,
        -330, 14006, 19345, -241,
        -323, 13512, 19799, -210,
        -314, 13020, 20244, -173,
        -304, 12531, 20679, -132,
        -293, 12045, 21104, -84,
        -280, 11564, 21517, -30,
        -268, 11088, 21917, 31,
        -254, 10618, 22304, 99,
        -240, 10153, 22678, 174,
        -226, 9696, 23036, 257,
        -212, 9246, 23379, 347,
        -198, 8804, 23706, 446,
        -184, 8371, 24016, 554,
        -170, 7947, 24309, 671,
        -156, 7532, 24583, 796,
        -143, 7127, 24838, 932,
        -130, 6732, 25075, 1077,
        -118, 6347, 25291, 1233,
        -106, 5974, 25487, 1399,
        -95, 5611, 25663, 1576,
        -84, 5260, 25817, 1764,
        -74, 4920, 25950, 1963,
        -65, 4592, 26061, 2173,
        -56, 4276, 26150, 2394,
        -48, 3971, 26217, 2628,
        -40, 3679, 26262, 2873,
        -33, 3398, 26285, 3129,
    };
    return table;
}

} // namespace lotus
