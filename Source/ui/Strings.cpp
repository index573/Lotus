#include "Strings.h"

#include <juce_data_structures/juce_data_structures.h>

#include <atomic>
#include <string>
#include <unordered_map>

namespace lotus::ui
{

namespace
{
struct Entry { const char* en; const char* ja; };

#include "StringsJa.inc"

std::atomic<int> current { -1 };
std::atomic<int> generation { 0 };

juce::PropertiesFile::Options settingsOptions()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "Lotus";
    o.filenameSuffix = "settings";
    o.folderName = "Lotus";
    o.osxLibrarySubFolder = "Application Support";
    o.millisecondsBeforeSaving = -1;
    return o;
}

const std::unordered_map<std::string, juce::String>& japanese()
{
    static const auto table = []
    {
        std::unordered_map<std::string, juce::String> m;
        for (const auto& e : japaneseEntries) m[e.en] = juce::String::fromUTF8 (e.ja);
        return m;
    }();
    return table;
}
} // namespace

Language language()
{
    int lang = current.load();
    if (lang < 0)
    {
        juce::PropertiesFile settings (settingsOptions());
        const auto saved = settings.getValue ("language");
        if (saved == "ja")      lang = (int) Language::Japanese;
        else if (saved == "en") lang = (int) Language::English;
        else lang = juce::SystemStats::getUserLanguage().startsWithIgnoreCase ("ja") ? (int) Language::Japanese : (int) Language::English;
        current = lang;
    }
    return (Language) lang;
}

void setLanguage (Language l)
{
    if ((int) l == current.load()) return;
    current = (int) l;
    ++generation;
    juce::PropertiesFile settings (settingsOptions());
    settings.setValue ("language", l == Language::Japanese ? "ja" : "en");
    settings.saveIfNeeded();
}

int languageGeneration() { language(); return generation.load(); }

juce::String tr (const char* english)
{
    if (language() == Language::Japanese)
    {
        const auto& t = japanese();
        const auto it = t.find (english);
        if (it != t.end()) return it->second;
    }
    return juce::String::fromUTF8 (english);
}

juce::String tr (const juce::String& english) { return tr (english.toRawUTF8()); }

juce::String trf (const char* english, std::initializer_list<std::pair<const char*, juce::String>> args)
{
    auto s = tr (english);
    for (const auto& [key, value] : args)
        s = s.replace ("{" + juce::String (key) + "}", value);
    return s;
}

juce::StringArray translatedKeys()
{
    juce::StringArray keys;
    for (const auto& e : japaneseEntries) keys.add (juce::String::fromUTF8 (e.en));
    return keys;
}

} // namespace lotus::ui
