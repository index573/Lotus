#include "VoiceRegistry.h"

namespace lotus
{

VoiceRegistry& VoiceRegistry::get()
{
    static VoiceRegistry registry;
    return registry;
}

int VoiceRegistry::join()
{
    for (int i = 0; i < maxInstances; ++i)
    {
        bool expected = false;
        if (slots[(size_t) i].used.compare_exchange_strong (expected, true))
        {
            slots[(size_t) i].voices = 0;
            slots[(size_t) i].console = -1;
            return i;
        }
    }
    return -1;
}

void VoiceRegistry::leave (int slot)
{
    if (slot < 0 || slot >= maxInstances) return;
    slots[(size_t) slot].voices = 0;
    slots[(size_t) slot].console = -1;
    slots[(size_t) slot].used = false;
}

int VoiceRegistry::publish (int slot, ConsoleId console, int voices)
{
    if (slot >= 0 && slot < maxInstances)
    {
        slots[(size_t) slot].console.store ((int) console, std::memory_order_relaxed);
        slots[(size_t) slot].voices.store (voices, std::memory_order_relaxed);
    }

    const int total = currentTotal (console);
    auto& peak = peaks[(size_t) console];
    int prev = peak.load (std::memory_order_relaxed);
    while (total > prev && ! peak.compare_exchange_weak (prev, total, std::memory_order_relaxed)) {}
    return total;
}

int VoiceRegistry::currentTotal (ConsoleId console) const
{
    int total = 0;
    for (const auto& s : slots)
        if (s.used.load (std::memory_order_relaxed) && s.console.load (std::memory_order_relaxed) == (int) console)
            total += s.voices.load (std::memory_order_relaxed);
    return total;
}

int VoiceRegistry::peakTotal (ConsoleId console) const
{
    return peaks[(size_t) console].load (std::memory_order_relaxed);
}

int VoiceRegistry::instancesOn (ConsoleId console) const
{
    int n = 0;
    for (const auto& s : slots)
        if (s.used.load (std::memory_order_relaxed) && s.console.load (std::memory_order_relaxed) == (int) console)
            ++n;
    return n;
}

std::array<bool, numConsoles> VoiceRegistry::consolesInUse() const
{
    std::array<bool, numConsoles> inUse {};
    for (const auto& s : slots)
    {
        if (! s.used.load (std::memory_order_relaxed)) continue;
        const int c = s.console.load (std::memory_order_relaxed);
        if (c >= 0 && c < numConsoles) inUse[(size_t) c] = true;
    }
    return inUse;
}

void VoiceRegistry::resetPeak (ConsoleId console)
{
    peaks[(size_t) console].store (currentTotal (console), std::memory_order_relaxed);
}

} // namespace lotus
