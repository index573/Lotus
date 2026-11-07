#!/usr/bin/env python3
"""Checks that every UI string passed to tr()/trf() (plus the names shown indirectly:
consoles, effect presets, captions, error messages) has a Japanese entry in
Source/ui/StringsJa.inc. Run from the repository root; exits 1 if anything is missing."""
import glob, re, sys

src = ""
for f in glob.glob("Source/**/*.cpp", recursive=True) + glob.glob("Source/**/*.h", recursive=True):
    src += open(f, encoding="utf-8").read() + "\n"

def literal_iter(text):
    # string literals inside tr( ... ) / trf ( ... ) first argument incl. ternaries
    for m in re.finditer(r'\btrf?\s*\(', text):
        i = m.end(); depth = 1; j = i
        while j < len(text) and depth > 0:
            c = text[j]
            if c == '"':
                k = j + 1
                while text[k] != '"':
                    if text[k] == '\\': k += 1
                    k += 1
                j = k
            elif c == '(': depth += 1
            elif c == ')': depth -= 1
            elif c == ',' and depth == 1: break
            j += 1
        arg = text[i:j]
        for lit in re.findall(r'"((?:[^"\\]|\\.)*)"', arg):
            yield bytes(lit, "utf-8").decode("unicode_escape").encode("latin-1").decode("utf-8")

keys = set(k for k in literal_iter(src) if k.strip())
# indirect keys
indirect = []
cons = open("Source/dsp/Consoles.cpp", encoding="utf-8").read()
for m in re.finditer(r'\{ "([^"]+)", "([^"]+)", "([^"]+)",', cons):
    indirect += [m.group(1), m.group(2), m.group(3)]
eff = open("Source/dsp/Effects.cpp", encoding="utf-8").read()
indirect += re.findall(r'\{ "([A-Z][A-Za-z ]+)", 0x', eff)                     # PS1 presets
indirect += re.findall(r'\{ "([A-Z][A-Za-z ]+)", \d+ \* ms', eff)              # N64 presets
indirect += re.findall(r'snesFirNames\[numSnesFirPresets\] = \{ (.*?) \};', eff)
ed = open("Source/PluginEditor.cpp", encoding="utf-8").read()
for m in re.finditer(r'\{ "([^"]+)", "([^"]+)", ParamIDs::\w+, lotus::ConsoleId::\w+,\s*"([^"]+)" \}', ed):
    indirect += [m.group(1), m.group(2), m.group(3)]
indirect += re.findall(r'one = "([^"]+)"', ed)
indirect += re.findall(r'Knob\s*\(p\.params, ParamIDs::\w+,\s*"([^"]+)"\)', ed)
for m in re.finditer(r'\(p\.params, ParamIDs::\w+,\s*\{ ([^}]*) \}\)', ed):
    indirect += re.findall(r'"([^"]+)"', m.group(1))
indirect += re.findall(r'Card \w+ \{ "([^"]+)" \}', open("Source/PluginEditor.h").read())
indirect += re.findall(r'StringArray \{ ([^}]*) \}', open("Source/PluginEditor.h").read())
for f in ["Source/dsp/SoundFont.cpp", "Source/dsp/TuningAnalysis.cpp"]:
    indirect += re.findall(r'error = "([^"]+)"', open(f).read())
indirect += ["Could not load SoundFont."]
flat = []
for k in indirect:
    flat += re.findall(r'"([^"]+)"', k) if '"' in k else [k]
keys |= set(x for x in flat if x.strip())

inc = open("Source/ui/StringsJa.inc", encoding="utf-8").read()
have = set()
for m in re.finditer(r'\{ "((?:[^"\\]|\\.)*)", "(?:[^"\\]|\\.)*" \}', inc):
    have.add(m.group(1).replace("\\n", "\n").replace('\\"', '"').replace("\\\\", "\\"))

missing = sorted(keys - have)
for k in missing:
    print("missing Japanese for:", repr(k))
print(f"{len(keys)} strings, {len(missing)} missing")
sys.exit(1 if missing else 0)
