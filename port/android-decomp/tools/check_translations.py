#!/usr/bin/env python3
"""The app menu's translations (app/src/main/res/values-<language>/strings_ingame.xml) against
the English texts (values/strings_ingame.xml), which Android shows for every language without a
file of its own (decision of the project owner, 21.10.2026).

For each language: every text of the English file is there and no other; each text has the same
placeholders (%1$s, %2$d, ...) and line breaks (\\n) as the English one; no apostrophe without
the backslash Android needs (\\'), no @ or ? at the start (Android would take it for a
reference), no special space such as U+00A0 as the character itself (written \\u00A0); each
plural has its "other" form. Languages written right to left (Arabic, ...): each text starts
right to left (its first letter, or the mark &#x200F; before Latin letters or a
file name; the FPS display excepted), "N × M" and ".gci" carry the left-to-right mark &#x200E;
that keeps their order. Also: every R.string.ingame_* / R.plurals.ingame_* the app uses is in
the English file, and res/xml/locales_config.xml (Android's language per app) lists English and
exactly the languages with a file.

    tools/check_translations.py [RES_DIR]

Exit code 1 on any error; prints one line per language.
"""

import re
import sys
import unicodedata
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RES = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "app/src/main/res"
NAME = "strings_ingame.xml"
JAVA = [ROOT / "app/src/main/java/com/ast/re4decomp" / f for f in ("CoreMenu.java", "CoreActivity.java")]
PLACEHOLDER = re.compile(r"%(?:\d+\$)?[-#+ 0,(]*\d*(?:\.\d+)?[a-zA-Z]")
LOCALES = "xml/locales_config.xml"
ANDROID = "{http://schemas.android.com/apk/res/android}"
# languages written right to left (Android's folder names)
RTL = {"ar", "fa", "he", "iw", "ur"}
# shown left to right in every language (the FPS display, CoreActivity)
LTR_TEXTS = {"ingame_fps", "ingame_fps_none", "ingame_fps_smooth"}
LRM, RLM = "\u200e", "\u200f"


def texts(path):
    """name -> text for <string>, name -> {quantity: text} for <plurals>."""
    root = ET.parse(path).getroot()
    strings, plurals = {}, {}
    for e in root:
        name = e.get("name")
        if e.tag == "string":
            strings[name] = "".join(e.itertext())
        elif e.tag == "plurals":
            plurals[name] = {i.get("quantity"): "".join(i.itertext()) for i in e if i.tag == "item"}
    return strings, plurals


def form(text):
    """What must match the English text: the placeholders (sorted) and the line breaks."""
    return sorted(PLACEHOLDER.findall(text.replace("%%", ""))), text.count("\\n")


def first_strong(text):
    """'L' or 'R': the direction Android lays a text out in (its first letter decides; a number is
    no letter, a file name %1$s is Latin)."""
    text = re.sub(r"%\d+\$s", "x", re.sub(r"%\d+\$d", "", text))
    for c in text:
        kind = unicodedata.bidirectional(c)
        if kind == "L":
            return "L"
        if kind in ("R", "AL"):
            return "R"
    return None


def rtl_problems(where, name, text):
    out = []
    if name not in LTR_TEXTS and first_strong(text) == "L":
        out.append(f"{where}: starts left to right; begin it with &#x200F; (right-to-left mark)")
    if re.search(f"(?<!{LRM}) ×|× (?!{LRM})", text):
        out.append(f"{where}: write N&#x200E; × &#x200E;M, else the numbers swap places")
    if re.search(f"(?<!{LRM})\\.gci", text):
        out.append(f"{where}: write &#x200E;.gci, else the dot moves behind it")
    return out


def text_problems(where, text):
    out = []
    if re.match(r"[@?]", text):
        out.append(f"{where}: starts with {text[0]} (write \\{text[0]})")
    for c in sorted({c for c in text if c != " " and unicodedata.category(c) == "Zs"}):
        out.append(f"{where}: the space U+{ord(c):04X} itself becomes a normal space in the build; write \\u{ord(c):04X}")
    quoted = len(text) >= 2 and text.startswith('"') and text.endswith('"')
    if not quoted and re.search(r"(?<!\\)'", text):
        out.append(f"{where}: apostrophe without backslash (write \\')")
    if not quoted and re.search(r'(?<!\\)"', text):
        out.append(f'{where}: quotation mark without backslash (write \\")')
    return out


def main():
    english = RES / "values" / NAME
    if not english.exists():
        print(f"no {english}")
        return 1
    en_strings, en_plurals = texts(english)
    errors = []
    for name, text in en_strings.items():
        errors += text_problems(f"values/{NAME} {name}", text)
    # the names the app uses
    used = set()
    for java in JAVA:
        used |= set(re.findall(r"R\.(?:string|plurals)\.(ingame_\w+)", java.read_text(encoding="utf-8")))
    for name in sorted(used - set(en_strings) - set(en_plurals)):
        errors.append(f"values/{NAME}: {name} is used by the app but missing")
    unused = sorted((set(en_strings) | set(en_plurals)) - used)
    languages = sorted(p.parent.name for p in RES.glob(f"values-*/{NAME}"))
    for lang in languages:
        path = RES / lang / NAME
        try:
            strings, plurals = texts(path)
        except ET.ParseError as e:
            errors.append(f"{lang}/{NAME}: not readable as XML ({e}); & and < must be written &amp; and &lt;")
            continue
        bad = []
        rtl = lang.split("-")[1] in RTL
        for name in sorted(set(en_strings) - set(strings)):
            bad.append(f"{lang}: {name} missing")
        for name in sorted(set(strings) - set(en_strings)):
            bad.append(f"{lang}: {name} is not in the English file")
        for name in sorted(set(en_strings) & set(strings)):
            if form(strings[name]) != form(en_strings[name]):
                bad.append(f"{lang}: {name}: placeholders or line breaks differ from the English text "
                           f"({form(strings[name])} instead of {form(en_strings[name])})")
            bad += text_problems(f"{lang} {name}", strings[name])
            if rtl:
                bad += rtl_problems(f"{lang} {name}", name, strings[name])
        for name in sorted(set(en_plurals) - set(plurals)):
            bad.append(f"{lang}: plurals {name} missing")
        for name in sorted(set(plurals) & set(en_plurals)):
            items = plurals[name]
            if "other" not in items:
                bad.append(f"{lang}: plurals {name} has no \"other\" form")
            for quantity, text in items.items():
                if form(text) != form(en_plurals[name]["other"]):
                    bad.append(f"{lang}: plurals {name} ({quantity}): placeholders differ from the English text")
                bad += text_problems(f"{lang} {name} ({quantity})", text)
                if rtl:
                    bad += rtl_problems(f"{lang} {name} ({quantity})", name, text)
        print(f"{lang}: {len(strings)} texts, {len(plurals)} plurals, " + ("ok" if not bad else f"{len(bad)} errors"))
        errors += bad
    # Android's language per app: English and every language with a file, no other
    config = RES / LOCALES
    if config.exists():
        listed = {e.get(ANDROID + "name").split("-")[0] for e in ET.parse(config).getroot()
                  if e.tag == "locale"}
        have = {"en"} | {lang.split("-")[1] for lang in languages}
        for code in sorted(have - listed):
            errors.append(f"{LOCALES}: {code} has texts but is not listed")
        for code in sorted(listed - have):
            errors.append(f"{LOCALES}: {code} is listed but has no values-{code}/{NAME}")
    else:
        errors.append(f"no {config}")
    print(f"English: {len(en_strings)} texts, {len(en_plurals)} plurals; languages: "
          + (", ".join(languages) if languages else "none")
          + (f"; not used by the app: {', '.join(unused)}" if unused else ""))
    for e in errors:
        print("  " + e)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
