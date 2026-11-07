#pragma once

// A tiny SoundFont built in memory, shared by the engine tests and the FL host test.

#include <cstdint>
#include <cstring>
#include <vector>

// ---------------------------------------------------------------------------------
// Minimal SF2 writer

struct Bytes
{
    std::vector<uint8_t> d;
    void u8 (int v) { d.push_back ((uint8_t) v); }
    void u16 (int v) { u8 (v & 0xFF); u8 ((v >> 8) & 0xFF); }
    void u32 (uint32_t v) { u16 ((int) (v & 0xFFFF)); u16 ((int) (v >> 16)); }
    void str (const char* s, size_t n) { for (size_t i = 0; i < n; ++i) u8 (i < std::strlen (s) ? s[i] : 0); }
    void id (const char* s) { str (s, 4); }
    void append (const Bytes& b) { d.insert (d.end(), b.d.begin(), b.d.end()); }
};

inline Bytes chunk (const char* id, const Bytes& body)
{
    Bytes b; b.id (id); b.u32 ((uint32_t) body.d.size()); b.append (body);
    if (body.d.size() & 1) b.u8 (0);
    return b;
}

inline Bytes list (const char* type, const std::vector<Bytes>& chunks)
{
    Bytes body; body.id (type);
    for (auto& c : chunks) body.append (c);
    return chunk ("LIST", body);
}

struct TestFont
{
    std::vector<uint8_t> data;
    uint32_t sampleLen = 0;
};

/** One preset ("Square", bank 0 prog 0) and a drum-ish preset (bank 0 prog 1) with
    an exclusive class. Sample: a looped 100-sample square wave, root key 69, 44.1 kHz. */
inline TestFont makeTestFont (int releaseTimecents = 2400 /* ~4 s: the "annoying" long release */)
{
    const uint32_t len = 1000, loopS = 100, loopE = 900;
    Bytes smpl;
    for (uint32_t i = 0; i < len + 46; ++i)
        smpl.u16 (i < len ? (((i / 50) % 2) ? 16000 : -16000) & 0xFFFF : 0);

    // phdr: Square, Kick, EOP
    Bytes phdr;
    phdr.str ("Square", 20); phdr.u16 (0); phdr.u16 (0); phdr.u16 (0); phdr.u32 (0); phdr.u32 (0); phdr.u32 (0);
    phdr.str ("Kit", 20);    phdr.u16 (1); phdr.u16 (0); phdr.u16 (1); phdr.u32 (0); phdr.u32 (0); phdr.u32 (0);
    phdr.str ("EOP", 20);    phdr.u16 (0); phdr.u16 (0); phdr.u16 (2); phdr.u32 (0); phdr.u32 (0); phdr.u32 (0);

    Bytes pbag; pbag.u16 (0); pbag.u16 (0); pbag.u16 (1); pbag.u16 (0); pbag.u16 (2); pbag.u16 (0);
    Bytes pmod; for (int i = 0; i < 10; ++i) pmod.u8 (0);
    Bytes pgen;
    pgen.u16 (41); pgen.u16 (0);   // preset 0 -> instrument 0
    pgen.u16 (41); pgen.u16 (1);   // preset 1 -> instrument 1
    pgen.u16 (0);  pgen.u16 (0);   // terminator

    Bytes inst;
    inst.str ("SquareInst", 20); inst.u16 (0);
    inst.str ("KitInst", 20);    inst.u16 (1);
    inst.str ("EOI", 20);        inst.u16 (3);

    // ibag 0: square zone; ibag 1, 2: two kit zones (keys 42 & 46, exclusive class 1)
    Bytes ibag; int g = 0;
    ibag.u16 (g); ibag.u16 (0); g += 3;
    ibag.u16 (g); ibag.u16 (0); g += 3;
    ibag.u16 (g); ibag.u16 (0); g += 3;
    ibag.u16 (g); ibag.u16 (0);
    Bytes imod; for (int i = 0; i < 10; ++i) imod.u8 (0);

    Bytes igen;
    igen.u16 (38); igen.u16 (releaseTimecents & 0xFFFF);  // releaseVolEnv
    igen.u16 (54); igen.u16 (1);                          // sampleModes: loop
    igen.u16 (53); igen.u16 (0);                          // sampleID
    igen.u16 (43); igen.u16 (42 | (42 << 8));             // keyRange 42
    igen.u16 (57); igen.u16 (1);                          // exclusiveClass 1
    igen.u16 (53); igen.u16 (0);
    igen.u16 (43); igen.u16 (46 | (46 << 8));             // keyRange 46
    igen.u16 (57); igen.u16 (1);
    igen.u16 (53); igen.u16 (0);
    igen.u16 (0);  igen.u16 (0);

    Bytes shdr;
    shdr.str ("square", 20); shdr.u32 (0); shdr.u32 (len); shdr.u32 (loopS); shdr.u32 (loopE); shdr.u32 (44100);
    shdr.u8 (69); shdr.u8 (0); shdr.u16 (0); shdr.u16 (1);
    shdr.str ("EOS", 20); for (int i = 0; i < 26; ++i) shdr.u8 (0);

    Bytes ifil; ifil.u16 (2); ifil.u16 (1);
    Bytes inam; inam.str ("Lotus Test", 12);

    Bytes body; body.id ("sfbk");
    body.append (list ("INFO", { chunk ("ifil", ifil), chunk ("INAM", inam) }));
    body.append (list ("sdta", { chunk ("smpl", smpl) }));
    body.append (list ("pdta", { chunk ("phdr", phdr), chunk ("pbag", pbag), chunk ("pmod", pmod), chunk ("pgen", pgen),
                                 chunk ("inst", inst), chunk ("ibag", ibag), chunk ("imod", imod), chunk ("igen", igen),
                                 chunk ("shdr", shdr) }));
    Bytes riff = chunk ("RIFF", body);
    return { riff.d, len };
}
