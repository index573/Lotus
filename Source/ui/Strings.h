#pragma once

// UI language. English strings are the keys; tr() returns the translation for the
// current language (or the English text if there isn't one). The choice is saved in the
// user's settings, so every Lotus instance shares it.

#include <juce_core/juce_core.h>

#include <initializer_list>
#include <utility>

namespace lotus::ui
{

enum class Language { English = 0, Japanese = 1 };

Language language();
void setLanguage (Language);
int languageGeneration();   // changes whenever the language does

juce::String tr (const char* english);
juce::String tr (const juce::String& english);

/** tr() with {placeholders} filled in, e.g. trf ("Analyzing... {done}/{total}", { { "done", "2" }, { "total", "83" } }). */
juce::String trf (const char* english, std::initializer_list<std::pair<const char*, juce::String>> args);

/** Every English key that has a translation (for tests). */
juce::StringArray translatedKeys();

} // namespace lotus::ui
