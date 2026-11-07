// Loads the FL Studio edition through the same entry point and voice calls FL uses, with a
// mock host standing in for FL. Checks slides (live FinalLevels.Pitch), note colours,
// parameters, state, and that finished voices are handed back to the host.
//
// Usage: lotus_fl_host_test path/to/Lotus_x64.so

#include "TestFont.h"

#include <dlfcn.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <vector>

#include "../Source/fl/sdk/fp_plugclass.h"

static int failures = 0;
#define CHECK(cond) do { if (! (cond)) { std::printf ("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

namespace
{

class MemoryStream final : public IStream
{
public:
    std::vector<uint8_t> data;
    size_t pos = 0;

    void QueryInterface() override {}
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE Read (void* pv, ULONG cb, ULONG* read) override
    {
        const size_t n = std::min<size_t> (cb, data.size() - pos);
        std::memcpy (pv, data.data() + pos, n);
        pos += n;
        if (read) *read = (ULONG) n;
        return 0;
    }
    HRESULT STDMETHODCALLTYPE Write (const void* pv, ULONG cb, ULONG* written) override
    {
        const auto* p = static_cast<const uint8_t*> (pv);
        data.insert (data.end(), p, p + cb);
        if (written) *written = cb;
        return 0;
    }
    HRESULT STDMETHODCALLTYPE Seek (LARGE_INTEGER, DWORD, ULARGE_INTEGER*) override { return 0; }
    HRESULT STDMETHODCALLTYPE SetSize (ULARGE_INTEGER) override { return 0; }
    HRESULT STDMETHODCALLTYPE CopyTo (IStream*, ULARGE_INTEGER, ULARGE_INTEGER*, ULARGE_INTEGER*) override { return 0; }
    HRESULT STDMETHODCALLTYPE Commit (DWORD) override { return 0; }
    HRESULT STDMETHODCALLTYPE Revert() override { return 0; }
    HRESULT STDMETHODCALLTYPE LockRegion (ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return 0; }
    HRESULT STDMETHODCALLTYPE UnlockRegion (ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return 0; }
    HRESULT STDMETHODCALLTYPE Stat (void*, DWORD) override { return 0; }
    HRESULT STDMETHODCALLTYPE Clone (IStream**) override { return 0; }
};

/** Plays FL's part: owns the voice parameters and answers the plugin's questions. */
class MockHost final : public TFruityPlugHost
{
public:
    TFruityPlug* plugin = nullptr;
    struct Note { TVoiceParams params {}; int colour = 0; TVoiceHandle handle = 0; bool alive = false; };
    std::map<intptr_t, Note> notes;
    intptr_t nextTag = 1;
    int paramChanges = 0, namesChanged = 0, killRequests = 0;

    MockHost() { std::memset (Reserved, 0, sizeof (Reserved)); HostVersion = 21000000; Flags = 0; AppHandle = 0; }

    // FL's levels for a key at the default root (C5 = 60): InitLevels.Pitch = 100 x key,
    // FinalLevels.Pitch = cents from the root.
    intptr_t trigger (int key, int colour, float velocity = 100.0f / 128.0f)
    {
        const intptr_t tag = nextTag++;
        auto& n = notes[tag];
        n.colour = colour;
        n.params.InitLevels = { 0.0f, velocity, 100.0f * key, 0.0f, 0.0f };
        n.params.FinalLevels = { 0.0f, 1.0f, 100.0f * (key - 60), 0.0f, 0.0f };
        velocityBits = velocity;
        n.handle = plugin->TriggerVoice (&n.params, tag);
        n.alive = true;
        return tag;
    }

    void release (intptr_t tag) { plugin->Voice_Release (notes[tag].handle); }

    std::vector<float> render (double seconds, double rate)
    {
        const int total = (int) (seconds * rate);
        std::vector<float> stereo ((size_t) total * 2, 0.0f), left ((size_t) total);
        for (int done = 0; done < total; )
        {
            int len = std::min (512, total - done);
            plugin->Gen_Render (reinterpret_cast<PWAV32FS> (stereo.data() + 2 * done), len);
            done += len;
        }
        for (int i = 0; i < total; ++i) left[(size_t) i] = stereo[(size_t) i * 2];
        return left;
    }

    intptr_t _stdcall Dispatcher (TPluginTag, intptr_t id, intptr_t index, intptr_t value) override
    {
        if (id == FHD_NamesChanged) ++namesChanged;
        if (id == FHD_GetColorRGBValue && index == CP_VoiceColors) return value < 0 ? 16 : 0x00A5D19E;
        return 0;
    }
    void _stdcall OnParamChanged (TPluginTag, int, int) override { ++paramChanges; }
    void _stdcall OnHint (TPluginTag, char*) override {}
    void _stdcall ComputeLRVol_Old (float&, float&, int, float) override {}
    void _stdcall Voice_Release (intptr_t) override {}
    void _stdcall Voice_Kill (intptr_t tag, BOOL) override
    {
        ++killRequests;
        auto it = notes.find (tag);
        if (it == notes.end() || ! it->second.alive) return;
        it->second.alive = false;
        plugin->Voice_Kill (it->second.handle);   // FL kills the plugin's voice in turn
    }
    int _stdcall Voice_ProcessEvent (intptr_t tag, intptr_t id, intptr_t, intptr_t) override
    {
        if (id == FPV_GetColor) return notes.count (tag) ? notes[tag].colour : 0;
        if (id == FPV_GetVelocity) { int r; std::memcpy (&r, &velocityBits, 4); return r; }
        return 0;
    }
    void _stdcall LockMix_Old() override {}
    void _stdcall UnlockMix_Old() override {}
    void _stdcall MIDIOut_Delayed (TPluginTag, intptr_t) override {}
    void _stdcall MIDIOut (TPluginTag, intptr_t) override {}
    void _stdcall AddWave_32FM_32FS_Ramp (void*, void*, int, float, float, float&, float&) override {}
    void _stdcall AddWave_32FS_32FS_Ramp (void*, void*, int, float, float, float&, float&) override {}
    bool _stdcall LoadSample (TSampleHandle&, char*, PWaveFormatExtensible, int) override { return false; }
    void* _stdcall GetSampleData (TSampleHandle, int&) override { return nullptr; }
    void _stdcall CloseSample (TSampleHandle) override {}
    int _stdcall GetSongMixingTime() override { return 0; }
    double _stdcall GetSongMixingTime_A() override { return 0; }
    double _stdcall GetSongPlayingTime() override { return 0; }
    void _stdcall OnControllerChanged (TPluginTag, intptr_t, intptr_t) override {}
    void* _stdcall GetSendBuffer (intptr_t) override { return nullptr; }
    void _stdcall PlugMsg_Delayed (TPluginTag, intptr_t) override {}
    void _stdcall PlugMsg_Kill (TPluginTag, intptr_t) override {}
    void _stdcall GetSampleInfo (TSampleHandle, PSampleInfo) override {}
    void _stdcall DistWave_32FM (int, int, void*, int, float, float, float) override {}
    void* _stdcall GetMixBuffer (int) override { return nullptr; }
    void* _stdcall GetInsBuffer (TPluginTag, int) override { return nullptr; }
    BOOL _stdcall PromptEdit (int, int, char*, char*, int&) override { return 0; }
    void _stdcall SuspendOutput_Old() override {}
    void _stdcall ResumeOutput_Old() override {}
    void _stdcall GetSampleRegion (TSampleHandle, int, PSampleRegion) override {}
    void _stdcall ComputeLRVol (float& l, float& r, float, float v) override { l = r = v; }
    void _stdcall LockPlugin (TPluginTag) override {}
    void _stdcall UnlockPlugin (TPluginTag) override {}
    void _stdcall LockMix_Shared_Old() override {}
    void _stdcall UnlockMix_Shared_Old() override {}
    void _stdcall GetInBuffer (TPluginTag, intptr_t, PIOBuffer) override {}
    void _stdcall GetOutBuffer (TPluginTag, intptr_t, PIOBuffer) override {}
    TOutVoiceHandle _stdcall TriggerOutputVoice (TVoiceParams*, intptr_t, intptr_t) override { return 0; }
    void _stdcall OutputVoice_Release (TOutVoiceHandle) override {}
    void _stdcall OutputVoice_Kill (TOutVoiceHandle) override {}
    int _stdcall OutputVoice_ProcessEvent (TOutVoiceHandle, intptr_t, intptr_t, intptr_t) override { return 0; }
    BOOL _stdcall PromptEdit_Ex (int, int, const char*, char*, int&, int&, int&, int, int) override { return 0; }
    void _stdcall SuspendOutput (TPluginTag) override {}
    void _stdcall ResumeOutput (TPluginTag) override {}

private:
    float velocityBits = 0.78f;
};

double zeroCrossFreq (const std::vector<float>& x, double rate)
{
    int crossings = 0;
    for (size_t i = 1; i < x.size(); ++i)
        if ((x[i - 1] < 0.0f) != (x[i] < 0.0f)) ++crossings;
    return crossings / 2.0 / ((double) x.size() / rate);
}

float peak (const std::vector<float>& x)
{
    float p = 0.0f;
    for (float v : x) p = std::max (p, std::fabs (v));
    return p;
}

std::string name (TFruityPlug* p, int section, int index, int value = 0)
{
    char buf[256] = {};
    p->GetName (section, index, value, buf);
    return buf;
}

int findParam (TFruityPlug* p, const std::string& wanted)
{
    for (int i = 0; i < p->Info->NumParams; ++i)
        if (name (p, FPN_Param, i) == wanted) return i;
    return -1;
}

} // namespace

int main (int argc, char** argv)
{
    if (argc < 2) { std::printf ("usage: %s Lotus_x64.so\n", argv[0]); return 2; }

    void* lib = dlopen (argv[1], RTLD_NOW | RTLD_LOCAL);
    if (lib == nullptr) { std::printf ("dlopen failed: %s\n", dlerror()); return 1; }
    using Create = TFruityPlug* (*) (TFruityPlugHost*, TPluginTag);
    auto create = reinterpret_cast<Create> (dlsym (lib, "CreatePlugInstance"));
    if (create == nullptr) { std::printf ("CreatePlugInstance not exported\n"); return 1; }

    const auto font = makeTestFont();
    const std::string fontPath = "/tmp/lotus_fl_test.sf2";
    std::ofstream (fontPath, std::ios::binary).write ((const char*) font.data.data(), (std::streamsize) font.data.size());

    constexpr double rate = 48000.0;
    MockHost host;
    TFruityPlug* plug = create (&host, 1234);
    CHECK (plug != nullptr);
    if (plug == nullptr) return 1;
    host.plugin = plug;

    std::printf ("plugin info\n");
    CHECK (plug->Info != nullptr && plug->Info->SDKVersion == CurrentSDKVersion);
    CHECK ((plug->Info->Flags & FPF_Type_FullGen) == FPF_Type_FullGen);
    CHECK (std::string (plug->Info->LongName) == "Lotus");
    CHECK (plug->Info->NumParams > 10);
    CHECK (plug->HostTag == 1234);

    plug->Dispatcher (FPD_SetSampleRate, 0, (intptr_t) rate);
    plug->Dispatcher (FPD_SetBlockSize, 0, 512);
    CHECK (plug->Dispatcher (FPD_LoadFile, LFT_DroppedFile, (intptr_t) fontPath.c_str()) == 1);

    std::printf ("parameters\n");
    const int noteOff = findParam (plug, "Note Off");
    CHECK (noteOff >= 0);
    CHECK (name (plug, FPN_ParamValue, noteOff, 65536) == "Cut");
    plug->ProcessParam (noteOff, 65536, REC_UpdateValue | REC_UpdateControl);
    CHECK (plug->ProcessParam (noteOff, 0, REC_GetValue) == 65536);
    CHECK (host.paramChanges == 0);   // FL's own changes aren't echoed back

    std::printf ("slides: FinalLevels.Pitch bends a playing note\n");
    const intptr_t a = host.trigger (69, 0);
    host.render (0.05, rate);
    auto before = host.render (0.25, rate);
    CHECK (std::fabs (zeroCrossFreq (before, rate) - 441.0) < 5.0);
    host.notes[a].params.FinalLevels.Pitch += 1200.0f;   // slide up an octave
    host.render (0.05, rate);
    auto after = host.render (0.25, rate);
    CHECK (std::fabs (zeroCrossFreq (after, rate) - 882.0) < 8.0);

    std::printf ("finished voices go back to FL\n");
    host.release (a);
    host.render (0.05, rate);
    CHECK (! host.notes[a].alive);
    CHECK (host.killRequests == 1);
    CHECK (peak (host.render (0.05, rate)) == 0.0f);

    std::printf ("state round trip with a note colour slot\n");
    MemoryStream saved;
    plug->SaveRestoreState (&saved, 1);
    CHECK (saved.data.size() > 16);
    // Give colour 3 (index 2) the kit: rewrite the saved slot list and load it back.
    std::string xml (saved.data.begin() + 16, saved.data.end());
    const auto slotAttr = std::regex ("colourPresets=\"[^\"]*\"");
    CHECK (std::regex_search (xml, slotAttr));
    xml = std::regex_replace (xml, slotAttr, "colourPresets=\"-,-,0:1\"");
    while (! xml.empty() && xml.back() == 0) xml.pop_back();
    MemoryStream edited;
    const int32_t version = 1;
    edited.Write (&version, 4, nullptr);
    const uint32_t outer = (uint32_t) (8 + xml.size() + 1);
    edited.Write (&outer, 4, nullptr);
    const uint32_t magic = 0x21324356, inner = (uint32_t) xml.size() + 1;
    edited.Write (&magic, 4, nullptr);
    edited.Write (&inner, 4, nullptr);
    edited.Write (xml.c_str(), (ULONG) xml.size() + 1, nullptr);
    const int namesBefore = host.namesChanged;
    plug->SaveRestoreState (&edited, 0);
    CHECK (host.namesChanged > namesBefore);
    CHECK (plug->ProcessParam (noteOff, 0, REC_GetValue) == 65536);

    std::printf ("note colours pick presets and name themselves\n");
    CHECK (name (plug, FPN_VoiceColor, 2) == "Kit");
    CHECK (name (plug, FPN_VoiceColor, 0) == "Square");
    CHECK (name (plug, FPN_Semitone, 42, 2) == "square");   // kit keys show their sample
    CHECK (name (plug, FPN_Semitone, 42, 0).empty());       // melodic presets keep FL's note names

    const intptr_t kit = host.trigger (42, 2);
    auto kitOut = host.render (0.1, rate);
    CHECK (peak (kitOut) > 0.05f);
    host.release (kit);
    host.render (0.05, rate);

    const int kills = host.killRequests;
    const intptr_t none = host.trigger (69, 2);   // the kit has nothing on key 69
    host.render (0.02, rate);
    CHECK (! host.notes[none].alive);
    CHECK (host.killRequests == kills + 1);

    std::printf ("many voices at once\n");
    std::vector<intptr_t> chord;
    for (int k = 48; k < 72; ++k) chord.push_back (host.trigger (k, 0));
    auto chordOut = host.render (0.05, rate);
    CHECK (peak (chordOut) > 0.05f);
    for (auto t : chord) host.release (t);
    host.render (0.1, rate);
    for (auto t : chord) CHECK (! host.notes[t].alive);

    plug->DestroyObject();
    dlclose (lib);
    std::remove (fontPath.c_str());

    if (failures == 0) std::printf ("\nAll FL host tests passed.\n");
    else std::printf ("\n%d check(s) failed.\n", failures);
    return failures == 0 ? 0 : 1;
}
