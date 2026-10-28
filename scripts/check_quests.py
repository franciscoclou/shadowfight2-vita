#!/usr/bin/env python3
"""Compare the quest files src/s3e/quests.c edits with the originals.

    scripts/check_quests.py <edited_dir> [ram_dir]

edited_dir comes from a harness run with SF_QUESTS_TEST (files named
assets_quest_extensions_zone_7_story.xml and so on); ram_dir defaults to
game_out/ram. For each file it checks that the result still parses, keeps
every quest (same names, same order), and lists each quest that changed:
"off" when it only gained the never-true condition, otherwise the
elements it lost. Exits non-zero on any structural difference.
"""
import sys
import xml.etree.ElementTree as ET
from collections import Counter
from pathlib import Path

NEVER = ("Equal", (("Value1", "vita_offline"), ("Value2", "1")))

edited = Path(sys.argv[1])
ram = Path(sys.argv[2] if len(sys.argv) > 2 else "game_out/ram")


def elements(e):
    return [(x.tag, tuple(sorted(x.attrib.items()))) for x in e.iter()]


bad = 0
for f in sorted(edited.glob("assets_*.xml")):
    rel = f.name.replace("assets_quest_extensions_", "assets/quest_extensions/").replace("assets_", "assets/", 1)
    if rel.startswith("assets/quest_extensions/zone_") or rel.startswith("assets/quest_extensions/intermission"):
        head, tail = rel.rsplit("_", 1)
        rel = head + "/" + tail
    try:
        new = ET.parse(f).getroot()
    except ET.ParseError as e:
        print(f"{rel}: DOES NOT PARSE: {e}")
        bad += 1
        continue
    old = ET.parse(ram / rel).getroot()
    oq, nq = list(old.iter("Quest")), list(new.iter("Quest"))
    if [q.get("Name") for q in oq] != [q.get("Name") for q in nq]:
        print(f"{rel}: QUEST LIST DIFFERS")
        bad += 1
        continue
    print(f"== {rel}: {len(nq)} quests")
    for a, b in zip(oq, nq):
        ea, eb = elements(a), elements(b)
        if ea == eb:
            continue
        gained = Counter(eb) - Counter(ea)
        lost = Counter(ea) - Counter(eb)
        if gained == Counter([NEVER]) and not lost:
            print(f"   off  {a.get('Name')}")
        elif not gained:
            what = ", ".join(f"{t}" + (f"[{dict(at).get('Text') or dict(at).get('Title') or ''}]" if at else "")
                             for (t, at), n in lost.items() for _ in range(n)
                             if t in ("Button", "Dialog", "CheckBox", "Greater", "RaidsButtonTutorial"))
            print(f"   cut  {a.get('Name')}: {what}")
        else:
            print(f"   ??   {a.get('Name')}: gained {dict(gained)}, lost {dict(lost)}")
            bad += 1
sys.exit(1 if bad else 0)
