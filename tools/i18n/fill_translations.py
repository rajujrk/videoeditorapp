#!/usr/bin/env python3
"""Fills untranslated messages in i18n/tve_<language>.ts from JSON files of translations:
    {"<English source>": {"fr": "...", "de": "...", "ja": "...", ...}, ...}
A plural message (numerus) takes a list of forms, or one text used for every form.
Existing translations are left alone, unless --replace is given (for corrections after a
review). Afterwards run check_translations.py.
    python3 tools/i18n/fill_translations.py work/*.json
    python3 tools/i18n/fill_translations.py --replace corrections.json
"""
import html
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
# Plural forms per language, as Qt counts them.
FORMS = {"ta": 2, "fr": 2, "de": 2, "es": 2, "hi": 2, "ja": 1, "ko": 1, "th": 1, "vi": 1, "zh_CN": 1}


def fill(ts_path, language, table, replace=False):
    text = ts_path.read_text(encoding="utf-8")
    filled = 0

    def message(m):
        nonlocal filled
        block = m.group(0)
        source = html.unescape(re.search(r"<source>(.*?)</source>", block, re.S).group(1))
        entry = table.get(source, {}).get(language)
        if entry is None or ('type="unfinished"' not in block and not replace):
            return block
        filled += 1
        if 'numerus="yes"' in block:
            forms = entry if isinstance(entry, list) else [entry] * FORMS.get(language, 2)
            inner = "".join(f"\n            <numerusform>{html.escape(f, quote=False)}</numerusform>" for f in forms)
            return re.sub(r'<translation[^>]*>.*?</translation>',
                          lambda _: f"<translation>{inner}\n        </translation>", block, flags=re.S)
        return re.sub(r'<translation[^>]*>.*?</translation>',
                      lambda _: f"<translation>{html.escape(entry, quote=False)}</translation>", block, flags=re.S)

    text = re.sub(r"<message[^>]*>.*?</message>", message, text, flags=re.S)
    ts_path.write_text(text, encoding="utf-8")
    return filled


if __name__ == "__main__":
    replace = "--replace" in sys.argv[1:]
    table = {}
    for name in [a for a in sys.argv[1:] if a != "--replace"]:
        for source, languages in json.loads(pathlib.Path(name).read_text(encoding="utf-8")).items():
            table.setdefault(source, {}).update(languages)
    for ts in sorted((ROOT / "i18n").glob("tve_*.ts")):
        language = ts.stem[4:]
        print(f"{ts.name}: {fill(ts, language, table, replace)} {'replaced' if replace else 'filled'}")
