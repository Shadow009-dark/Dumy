"""Generates knowledge/REVIEW.md from common_sense.json and moves advice-like
entries into knowledge/pending_review.json (disabled).

Run after the suite so the runtime file is not changed mid-run:
    py tests/build_review.py
"""
import json
import os
import sys

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ACTIVE = os.path.join(ROOT, "knowledge", "common_sense.json")
PENDING = os.path.join(ROOT, "knowledge", "pending_review.json")
REVIEW = os.path.join(ROOT, "knowledge", "REVIEW.md")

# Entries that give advice or duplicate the PDFs; kept but disabled.
PARK = {"calf_colostrum", "milk_yield_factors"}
# Topic families that also exist in the indexed books.
PDF_OVERLAP = {
    "egg_incubation", "rumination", "cow_chewing_cud", "milk_color_yellow",
    "sheep_wool_growth", "cow_rumen_gas", "milk_yield_factors",
    "calf_colostrum", "sheep_tail_fat", "cattle_sleep",
}
HAS_DIGITS = {"egg_incubation", "cow_chewing_cud", "cattle_sleep",
              "chicken_memory_faces", "sheep_wool_growth", "bee_waggle"}


def main():
    data = json.load(open(ACTIVE, encoding="utf-8-sig"))
    entries = data.get("entries", [])
    active, parked = [], []
    for e in entries:
        (parked if e.get("id") in PARK else active).append(e)

    data["entries"] = active
    json.dump(data, open(ACTIVE, "w", encoding="utf-8"), ensure_ascii=False, indent=2)
    json.dump({"version": 1, "disabled": True, "entries": parked},
              open(PENDING, "w", encoding="utf-8"), ensure_ascii=False, indent=2)

    lines = ["# Curated knowledge review", "",
             f"Active entries: {len(active)}; parked in `pending_review.json`: {len(parked)}.", "",
             "Rules: no dosage, timing, quantity or treatment advice; digits only for",
             "stable well-known facts; hedge anything uncertain.", "",
             "| id | example triggers | answer | tags | safe | PDF overlap | digits |",
             "|---|---|---|---|---|---|---|"]
    for e in active + parked:
        trig = ", ".join(e.get("keywords", [])[:4])
        if e.get("combos"):
            trig += " + combo " + " × ".join("/".join(g[:3]) for g in e["combos"])
        ans = e.get("answer", "").replace("|", "\\|")
        tags = ", ".join(e.get("tags", []))
        lines.append("| {id} | {trig} | {ans} | {tags} | {safe} | {ov} | {dg} |".format(
            id=e.get("id"), trig=trig, ans=ans, tags=tags,
            safe=e.get("safe", True),
            ov="yes" if e.get("id") in PDF_OVERLAP else "no",
            dg="yes" if e.get("id") in HAS_DIGITS else "no"))
    lines += ["", "## Parked (disabled)", ""]
    for e in parked:
        lines.append(f"- **{e['id']}** — {e['answer']}")
    open(REVIEW, "w", encoding="utf-8").write("\n".join(lines) + "\n")
    print(f"wrote {REVIEW}: {len(active)} active, {len(parked)} parked")


if __name__ == "__main__":
    main()
