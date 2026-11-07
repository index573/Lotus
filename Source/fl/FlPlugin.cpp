// Lotus as a native FL Studio generator. FL drives every note through its own voice
// system, so slides, portamento and per-note pitch arrive as live voice levels, and each
// note carries one of the piano roll's 16 colours. VST plugins only get MIDI, which is
// why this edition exists.

#include "FlBridge.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <mutex>

#if defined (_WIN32)
 #define NOMINMAX
 #include <windows.h>
#endif
#include "sdk/fp_plugclass.h"   // last: it #defines BOOL, HWND, NULL...

namespace
{

constexpr int stateVersion = 1;
constexpr int maxFlVoices = 512;

char longName[] = "Lotus";
char shortName[] = "Lotus";
TFruityPlugInfo pluginInfo {};

float floatFromResult (intptr_t r)
{
    // FL returns floats typecast into the integer result.
    const auto bits = (uint32_t) (uintptr_t) r;
    float f;
    std::memcpy (&f, &bits, sizeof (f));
    return f;
}

class LotusFlPlugin final : public TFruityPlug, private lotus::fl::Bridge::Host
{
public:
    LotusFlPlugin (TFruityPlugHost* h, TPluginTag tag) : flHost (h)
    {
        HostTag = tag;
        bridge = lotus::fl::Bridge::create (*this);

        if (pluginInfo.SDKVersion == 0)
        {
            pluginInfo.SDKVersion = CurrentSDKVersion;
            pluginInfo.LongName = longName;
            pluginInfo.ShortName = shortName;
            pluginInfo.Flags = FPF_Type_FullGen;
           #if defined (__APPLE__)
            pluginInfo.Flags |= FPF_MacNeedsNSView;
           #endif
            pluginInfo.NumParams = bridge->numParams();
            pluginInfo.DefPoly = 0;
        }
        Info = &pluginInfo;
        bridge->prepare (sampleRate, blockSize);
    }

    ~LotusFlPlugin() override
    {
        bridge->closeEditor();
        bridge.reset();
    }

    // --- Dispatcher ---------------------------------------------------------------------

    intptr_t _stdcall Dispatcher (intptr_t id, intptr_t index, intptr_t value) override
    {
        switch (id)
        {
            case FPD_ShowEditor:
                if (value == 0)
                {
                    bridge->closeEditor();
                    EditorHandle = 0;
                }
                else
                {
                    EditorHandle = (HWND) (intptr_t) bridge->openEditor ((void*) value);
                }
                return 0;

            case FPD_SetSampleRate:
            {
                const std::lock_guard<std::recursive_mutex> lock (mutex);
                sampleRate = value > 0 ? (double) value : 44100.0;
                bridge->prepare (sampleRate, blockSize);
                return 0;
            }

            case FPD_SetBlockSize:
            {
                const std::lock_guard<std::recursive_mutex> lock (mutex);
                blockSize = std::max (32, (int) value);
                bridge->prepare (sampleRate, blockSize);
                return 0;
            }

            case FPD_LoadFile:
                if (index == LFT_DownloadDataPack) return 0;
                return value != 0 && bridge->loadFile ((const char*) value) ? 1 : 0;

            case FPD_ColorsHaveChanged:
                if (index == CP_VoiceColors) bridge->refreshNoteColours();
                return 0;

            default:
                return 0;
        }
    }

    void _stdcall Idle_Public() override {}

    // --- State -----------------------------------------------------------------------------

    void _stdcall SaveRestoreState (IStream* stream, BOOL save) override
    {
        if (stream == nullptr) return;
        if (save)
        {
            const auto data = bridge->saveState();
            const int32_t version = stateVersion;
            const uint32_t size = (uint32_t) data.size();
            stream->Write (&version, sizeof (version), nullptr);
            stream->Write (&size, sizeof (size), nullptr);
            if (size > 0) stream->Write (data.data(), size, nullptr);
            return;
        }

        int32_t version = 0;
        uint32_t size = 0;
        ULONG got = 0;
        if (stream->Read (&version, sizeof (version), &got) != 0 || got != sizeof (version) || version < 1) return;
        if (stream->Read (&size, sizeof (size), &got) != 0 || got != sizeof (size) || size == 0 || size > (1u << 28)) return;
        std::vector<uint8_t> data (size);
        if (stream->Read (data.data(), size, &got) != 0 || got != size) return;
        bridge->loadState (data.data(), data.size());
    }

    // --- Names -----------------------------------------------------------------------------

    void _stdcall GetName (int section, int index, int value, char* name) override
    {
        if (name == nullptr) return;
        std::string text;
        switch (section)
        {
            case FPN_Param:      text = bridge->paramName (index); break;
            case FPN_ParamValue: text = bridge->paramText (index, value); break;
            case FPN_VoiceColor: text = bridge->colourName (index); break;
            case FPN_Semitone:   text = bridge->keyName (index, value); break;
            default: break;
        }
        const size_t n = std::min<size_t> (text.size(), 255);
        std::memcpy (name, text.data(), n);
        name[n] = 0;
    }

    // --- Parameters --------------------------------------------------------------------------

    int _stdcall ProcessEvent (int, int, int) override { return 0; }

    int _stdcall ProcessParam (int index, int value, int flags) override
    {
        if (index < 0 || index >= bridge->numParams()) return 0;
        // Lotus' own range is FL's 0..65536, so values from MIDI need no translation.
        if (flags & REC_GetValue) value = bridge->getParam (index);
        if (flags & REC_UpdateValue) bridge->setParam (index, value);
        return value;
    }

    // --- Audio and voices --------------------------------------------------------------------

    void _stdcall Eff_Render (PWAV32FS, PWAV32FS, int) override {}

    void _stdcall Gen_Render (PWAV32FS dest, int& length) override
    {
        const std::lock_guard<std::recursive_mutex> lock (mutex);
        if (length <= 0 || dest == nullptr) return;

        // FL updates FinalLevels continuously (slides, portamento, the channel's knobs).
        for (auto& v : voices)
            if (v.used)
                bridge->noteLevels (v.note, pitchOffset (v), v.params->FinalLevels.Vol, v.params->FinalLevels.Pan);

        if ((int) left.size() < length) { left.resize ((size_t) length); right.resize ((size_t) length); }
        bridge->render (left.data(), right.data(), length);

        float* out = &dest[0][0];   // interleaved stereo
        for (int i = 0; i < length; ++i)
        {
            out[2 * i]     += left[(size_t) i];
            out[2 * i + 1] += right[(size_t) i];
        }

        // Voices whose sound has finished go back to FL.
        for (auto& v : voices)
            if (v.used && ! v.killRequested && ! bridge->isNoteSounding (v.note))
            {
                v.killRequested = true;
                flHost->Voice_Kill (v.tag, 1);
            }
    }

    TVoiceHandle _stdcall TriggerVoice (PVoiceParams params, intptr_t tag) override
    {
        const std::lock_guard<std::recursive_mutex> lock (mutex);
        Voice* v = freeVoice();
        if (v == nullptr || params == nullptr) return (TVoiceHandle) &spare;

        // Velocity drives Lotus' own curve; FL's volume then carries only the channel level.
        flHost->Voice_ProcessEvent (tag, FPV_SetLinkVelocity, 0, 0);
        const int colour = (int) flHost->Voice_ProcessEvent (tag, FPV_GetColor, 0, 0) & 15;
        const float vel = floatFromResult (flHost->Voice_ProcessEvent (tag, FPV_GetVelocity, 0, 0));
        const int velocity = std::isfinite (vel) && vel > 0.0f ? std::clamp ((int) std::lround (vel * 128.0f), 1, 127) : 100;

        v->used = true;
        v->killRequested = false;
        v->tag = tag;
        v->params = params;
        v->key = std::clamp ((int) std::lround (params->InitLevels.Pitch / 100.0f), 0, 127);
        v->note = bridge->noteOn (v->key, velocity, colour, pitchOffset (*v), params->FinalLevels.Vol, params->FinalLevels.Pan);
        return (TVoiceHandle) v;
    }

    void _stdcall Voice_Release (TVoiceHandle handle) override
    {
        const std::lock_guard<std::recursive_mutex> lock (mutex);
        if (auto* v = voiceFor (handle)) bridge->noteOff (v->note);
    }

    void _stdcall Voice_Kill (TVoiceHandle handle) override
    {
        const std::lock_guard<std::recursive_mutex> lock (mutex);
        if (auto* v = voiceFor (handle))
        {
            bridge->noteKill (v->note);
            *v = Voice {};
        }
    }

    int _stdcall Voice_ProcessEvent (TVoiceHandle, intptr_t, intptr_t, intptr_t) override { return 0; }
    int _stdcall Voice_Render (TVoiceHandle, PWAV32FS, int&) override { return FVR_NoMoreData; }

    void _stdcall NewTick() override {}
    void _stdcall MIDITick() override {}
    void _stdcall MIDIIn (int&) override {}
    void _stdcall MsgIn (intptr_t) override {}
    int _stdcall OutputVoice_ProcessEvent (TOutVoiceHandle, intptr_t, intptr_t, intptr_t) override { return 0; }
    void _stdcall OutputVoice_Kill (TVoiceHandle) override {}

private:
    struct Voice
    {
        bool used = false, killRequested = false;
        intptr_t tag = 0;
        PVoiceParams params = nullptr;
        int key = 60;
        uint32_t note = 0;
    };

    // FinalLevels.Pitch is in cents with 0 at the channel's root key (C5 by default), so a
    // note keeps its key and the difference becomes a live bend.
    static float pitchOffset (const Voice& v)
    {
        return v.params->FinalLevels.Pitch + 6000.0f - 100.0f * (float) v.key;
    }

    Voice* freeVoice()
    {
        for (auto& v : voices)
            if (! v.used) return &v;
        return nullptr;
    }

    Voice* voiceFor (TVoiceHandle handle)
    {
        auto* v = reinterpret_cast<Voice*> (handle);
        if (v < voices.data() || v >= voices.data() + voices.size() || ! v->used) return nullptr;
        return v;
    }

    // Bridge::Host
    void paramChanged (int index, int value) override { flHost->OnParamChanged (HostTag, index, value); }

    void namesChanged() override
    {
        flHost->Dispatcher (HostTag, FHD_NamesChanged, 0, FPN_VoiceColor);
        flHost->Dispatcher (HostTag, FHD_NamesChanged, 0, FPN_Semitone);
    }

    void editorResized() override { flHost->Dispatcher (HostTag, FHD_EditorResized, 0, 0); }

    bool noteColourRgb (int index, uint8_t& r, uint8_t& g, uint8_t& b) override
    {
        if (flHost->Dispatcher (HostTag, FHD_GetColorRGBValue, CP_VoiceColors, -1) < 16) return false;
        // A Windows/Delphi colour value: 0x00BBGGRR.
        const auto c = (uint32_t) flHost->Dispatcher (HostTag, FHD_GetColorRGBValue, CP_VoiceColors, index);
        r = (uint8_t) (c & 0xFF);
        g = (uint8_t) ((c >> 8) & 0xFF);
        b = (uint8_t) ((c >> 16) & 0xFF);
        return true;
    }

    TFruityPlugHost* flHost;
    std::unique_ptr<lotus::fl::Bridge> bridge;
    std::recursive_mutex mutex;   // FL calls voices and rendering from more than one thread
    std::array<Voice, maxFlVoices> voices {};
    Voice spare;                  // handed out if all slots are taken; plays nothing
    std::vector<float> left, right;
    double sampleRate = 44100.0;
    int blockSize = 512;
};

} // namespace

TFruityPlug::TFruityPlug() : HostTag (0), Info (nullptr), EditorHandle (0), MonoRender (false)
{
    std::memset (&Reserved, 0, sizeof (Reserved));
}

TFruityPlug::~TFruityPlug() = default;

void _stdcall TFruityPlug::DestroyObject()
{
    delete this;
}

#if defined (_WIN32)
 #define LOTUS_FL_EXPORT extern "C" __declspec (dllexport)
#else
 #define LOTUS_FL_EXPORT extern "C" __attribute__ ((visibility ("default")))
#endif

LOTUS_FL_EXPORT TFruityPlug* _stdcall CreatePlugInstance (TFruityPlugHost* host, TPluginTag tag)
{
    return host != nullptr ? new LotusFlPlugin (host, tag) : nullptr;
}
