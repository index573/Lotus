#pragma once

// Every Lotus instance loaded in the same process (normally: the same DAW) signs in
// here and publishes how many voices it's playing and which console it targets. That
// gives each instance a song-wide view: the total voices across all instances aimed at
// the same console, and the peak since it was last reset. Lock-free: the audio thread
// only uses atomics.

#include "Consoles.h"

#include <array>
#include <atomic>
#include <cstddef>

namespace lotus
{

class VoiceRegistry
{
public:
    static constexpr int maxInstances = 128;

    static VoiceRegistry& get();

    /** Returns a slot id, or -1 if all slots are taken. */
    int join();
    void leave (int slot);

    /** Audio thread: publish this instance's state; returns the song-wide total for its console. */
    int publish (int slot, ConsoleId console, int voices);

    int currentTotal (ConsoleId console) const;
    int peakTotal (ConsoleId console) const;
    int instancesOn (ConsoleId console) const;
    void resetPeak (ConsoleId console);

    /** Which consoles the instances in this process target (instances that haven't played yet don't count). */
    std::array<bool, numConsoles> consolesInUse() const;

private:
    struct Slot
    {
        std::atomic<bool> used { false };
        std::atomic<int> console { -1 };   // -1 until the instance first publishes
        std::atomic<int> voices { 0 };
    };

    std::array<Slot, maxInstances> slots;
    std::array<std::atomic<int>, numConsoles> peaks {};
};

} // namespace lotus
