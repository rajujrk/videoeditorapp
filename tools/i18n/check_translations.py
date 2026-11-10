#!/usr/bin/env python3
"""Checks each translation in i18n/*.ts: every message is translated (none unfinished or
left over), keeps the source's %1, %2… placeholders, and its leading and trailing spaces
(the pieces put together around it rely on them).
    python3 tools/i18n/check_translations.py
"""
import pathlib
import re
import sys
import xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parents[2]


def problems(ts):
    for context in ET.parse(ts).getroot().iter("context"):
        name = context.findtext("name")
        for message in context.iter("message"):
            source = message.findtext("source") or ""
            translation = message.find("translation")
            text = translation.text or ""
            where = f"{ts.name} [{name}] {source!r}"
            if translation.get("type") in ("unfinished", "obsolete", "vanished") or not text:
                yield f"{where}: not translated"
            elif sorted(re.findall(r"%\d", source)) != sorted(re.findall(r"%\d", text)):
                yield f"{where}: placeholders differ in {text!r}"
            elif source[:1].isspace() != text[:1].isspace() or source[-1:].isspace() != text[-1:].isspace():
                yield f"{where}: spaces at the ends differ in {text!r}"


if __name__ == "__main__":
    found = [p for ts in sorted((ROOT / "i18n").glob("*.ts")) for p in problems(ts)]
    print("\n".join(found) or "translations complete")
    sys.exit(1 if found else 0)
