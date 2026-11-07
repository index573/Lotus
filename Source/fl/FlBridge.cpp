#if defined (_WIN32)
 #define JUCE_CORE_INCLUDE_NATIVE_HEADERS 1   // windows.h, for DllMain below
#endif

#include "FlBridge.h"

#include "../PluginProcessor.h"
#include "../ui/Widgets.h"

namespace lotus::fl
{

namespace
{
thread_local bool settingFromHost = false;

class BridgeImpl final : public Bridge,
                         private juce::AudioProcessorParameter::Listener,
                         private juce::ComponentListener
{
public:
    explicit BridgeImpl (Host& h) : host (h)
    {
        processor = std::make_unique<LotusProcessor>();
        for (auto* p : processor->getParameters())
            p->addListener (this);
        processor->onNamesChanged = [this] { host.namesChanged(); };
        refreshNoteColours();
    }

    ~BridgeImpl() override
    {
        closeEditor();
        for (auto* p : processor->getParameters())
            p->removeListener (this);
        processor->onNamesChanged = nullptr;
        processor.reset();
    }

    void prepare (double sampleRate, int maxBlock) override
    {
        rate = sampleRate > 0 ? sampleRate : 44100.0;
        block = juce::jmax (32, maxBlock);
        processor->setPlayConfigDetails (0, 2, rate, block);
        processor->prepareToPlay (rate, block);
        prepared = true;
    }

    void render (float* left, float* right, int numSamples) override
    {
        if (! prepared) prepare (rate, block);
        while (numSamples > 0)
        {
            const int n = juce::jmin (numSamples, block);
            float* channels[] = { left, right };
            juce::AudioBuffer<float> buffer (channels, 2, n);
            processor->processBlock (buffer, midi);
            left += n; right += n; numSamples -= n;
        }
    }

    uint32_t noteOn (int key, int velocity, int colour, float cents, float gain, float pan) override
    {
        return processor->hostNoteOn (key, velocity, colour, cents, gain, pan);
    }
    void noteLevels (uint32_t id, float cents, float gain, float pan) override { processor->hostNoteLevels (id, cents, gain, pan); }
    void noteOff (uint32_t id) override { processor->hostNoteOff (id); }
    void noteKill (uint32_t id) override { processor->hostNoteKill (id); }
    bool isNoteSounding (uint32_t id) const override { return processor->isNoteSounding (id); }

    int numParams() const override { return processor->getParameters().size(); }

    juce::AudioProcessorParameter* param (int index) const
    {
        return juce::isPositiveAndBelow (index, numParams()) ? processor->getParameters()[index] : nullptr;
    }

    std::string paramName (int index) const override
    {
        auto* p = param (index);
        return p != nullptr ? p->getName (255).toStdString() : std::string();
    }

    std::string paramText (int index, int value) const override
    {
        auto* p = param (index);
        if (p == nullptr) return {};
        auto text = p->getText ((float) value / 65536.0f, 64);
        if (p->getLabel().isNotEmpty()) text << " " << p->getLabel();
        return text.toStdString();
    }

    int getParam (int index) const override
    {
        auto* p = param (index);
        return p != nullptr ? juce::roundToInt (p->getValue() * 65536.0f) : 0;
    }

    void setParam (int index, int value) override
    {
        if (auto* p = param (index))
        {
            const juce::ScopedValueSetter<bool> guard (settingFromHost, true);
            p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, (float) value / 65536.0f));
        }
    }

    std::vector<uint8_t> saveState() override
    {
        juce::MemoryBlock mb;
        processor->getStateInformation (mb);
        const auto* d = static_cast<const uint8_t*> (mb.getData());
        return std::vector<uint8_t> (d, d + mb.getSize());
    }

    void loadState (const void* data, size_t size) override
    {
        processor->setStateInformation (data, (int) size);
    }

    bool loadFile (const std::string& path) override
    {
        const juce::File file (juce::String::fromUTF8 (path.c_str()));
        if (! file.hasFileExtension ("sf2")) return false;
        return processor->loadSoundFont (file, true).isEmpty();
    }

    std::string colourName (int colour) const override
    {
        if (! processor->hasColourPresets()) return {};   // FL keeps its own names until colours are used
        return processor->getColourPresetName (colour).toStdString();
    }

    std::string keyName (int key, int colour) const override
    {
        return processor->getKeyName (key, colour).toStdString();
    }

    void refreshNoteColours() override
    {
        for (int i = 0; i < 16; ++i)
        {
            uint8_t r = 0, g = 0, b = 0;
            if (host.noteColourRgb (i, r, g, b))
                lotus::ui::setHostNoteColour (i, juce::Colour (r, g, b));
        }
    }

    void* openEditor (void* parent) override
    {
        if (editor == nullptr)
        {
            editor.reset (processor->createEditorIfNeeded());
            if (editor == nullptr) return nullptr;
            editor->addComponentListener (this);
        }
        editor->setVisible (true);
        editor->addToDesktop (0, parent);
        return editor->getWindowHandle();
    }

    void closeEditor() override
    {
        if (editor == nullptr) return;
        editor->removeComponentListener (this);
        editor.reset();   // tells the processor the editor is gone
    }

private:
    void parameterValueChanged (int index, float newValue) override
    {
        if (! settingFromHost)
            host.paramChanged (index, juce::roundToInt (newValue * 65536.0f));
    }
    void parameterGestureChanged (int, bool) override {}

    void componentMovedOrResized (juce::Component&, bool, bool wasResized) override
    {
        if (wasResized) host.editorResized();
    }

    juce::ScopedJuceInitialiser_GUI juceInit;   // first member: JUCE is up before the processor exists
    Host& host;
    std::unique_ptr<LotusProcessor> processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    juce::MidiBuffer midi;
    double rate = 44100.0;
    int block = 512;
    bool prepared = false;
};
} // namespace

std::unique_ptr<Bridge> Bridge::create (Host& host)
{
    return std::make_unique<BridgeImpl> (host);
}

} // namespace lotus::fl

#if JUCE_WINDOWS
// JUCE needs this DLL's module handle (window classes, resources), not FL's.
extern "C" BOOL WINAPI DllMain (HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
        juce::Process::setCurrentModuleInstanceHandle (instance);
    return TRUE;
}
#endif
