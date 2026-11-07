#pragma once

// The part of Lotus the FL Studio wrapper talks to. It is implemented on the JUCE side
// (FlBridge.cpp) so the SDK headers, which #define BOOL, HWND and friends, never meet
// JUCE's headers in the same file.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lotus::fl
{

class Bridge
{
public:
    /** Calls back into FL Studio. */
    struct Host
    {
        virtual ~Host() = default;
        virtual void paramChanged (int index, int value) = 0;   // value 0..65536
        virtual void namesChanged() = 0;                        // colour / key names
        virtual void editorResized() = 0;
        virtual bool noteColourRgb (int index, uint8_t& r, uint8_t& g, uint8_t& b) = 0;
    };

    static std::unique_ptr<Bridge> create (Host&);
    virtual ~Bridge() = default;

    virtual void prepare (double sampleRate, int maxBlock) = 0;
    virtual void render (float* left, float* right, int numSamples) = 0;

    // Notes. pitchCents is the live offset from `key`; gain and pan come from FL's levels.
    virtual uint32_t noteOn (int key, int velocity, int colour, float pitchCents, float gain, float pan) = 0;
    virtual void noteLevels (uint32_t id, float pitchCents, float gain, float pan) = 0;
    virtual void noteOff (uint32_t id) = 0;
    virtual void noteKill (uint32_t id) = 0;
    virtual bool isNoteSounding (uint32_t id) const = 0;

    // Parameters, in FL's 0..65536 range.
    virtual int numParams() const = 0;
    virtual std::string paramName (int index) const = 0;
    virtual std::string paramText (int index, int value) const = 0;
    virtual int  getParam (int index) const = 0;
    virtual void setParam (int index, int value) = 0;

    virtual std::vector<uint8_t> saveState() = 0;
    virtual void loadState (const void* data, size_t size) = 0;
    virtual bool loadFile (const std::string& path) = 0;

    virtual std::string colourName (int colour) const = 0;   // the preset a colour plays
    virtual std::string keyName (int key, int colour) const = 0;
    virtual void refreshNoteColours() = 0;                     // re-read FL's palette

    /** Opens the editor inside FL's window (HWND or NSView). Returns the editor's native handle. */
    virtual void* openEditor (void* parent) = 0;
    virtual void closeEditor() = 0;
};

} // namespace lotus::fl
