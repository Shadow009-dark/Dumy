"""F6 probe: positives, negatives, action phrasings and the formerly failing cases."""
import json
import sys
import time
import urllib.request

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
BASE = "http://127.0.0.1:8085"


def ask(q, sid=""):
    body = json.dumps({"message": q, "session_id": "probe-" + sid}).encode()
    req = urllib.request.Request(BASE + "/chat", data=body,
                                 headers={"Content-Type": "application/json"})
    t = time.time()
    j = json.loads(urllib.request.urlopen(req, timeout=150).read())
    return time.time() - t, j


def main():
    cases = json.load(open("tests/expected/cases.json", encoding="utf-8-sig"))["cases"]
    by = {c["id"]: c["question"] for c in cases}

    print("== positives")
    for q in ["Cücə niyə bənövşəyi rəngdədir?", "Cücə bənövşəyidir", "cuce benovseyi",
              "Cücəni niyə boyayırlar?", "Mavi cücə varmı?"]:
        dt, j = ask(q, "p")
        src = (j.get("sources") or [{}])[0].get("type")
        print(f"{dt:.1f}s {j['route']} {src} boya={'boya' in j['answer'].lower()} | {q[:30]}")

    print("== negatives (must not be the dye answer)")
    for q in ["Cücə sarıdır", "Sarı cücə normaldırmı?", "Toyuq qırmızı rəngdədir",
              "Qara inək", "Ağ quzu", "Qonur at"]:
        dt, j = ask(q, "n")
        flag = "DYE!" if "süni boya" in j["answer"] else "ok"
        print(f"{dt:.1f}s {j['route']} {flag} | {q} -> {j['answer'][:50]}")

    print("== action")
    for q in ["Cücə bənövşəyidir, nə edə bilərəm?", "Cücə bənövşəyidir nə etməli",
              "cuce benovseyi ne edim"]:
        dt, j = ask(q, "a")
        print(f"{dt:.1f}s {j['route']} | {q[:34]} -> {j['answer'][:70]}")

    print("== formerly failing cases")
    for cid in ["t10_capital", "t16_pdf_diagnosis", "dom01_sheep", "dom02_milkfat",
                "q11", "q12"]:
        q = by.get(cid)
        if not q:
            print(cid, "missing")
            continue
        dt, j = ask(q, "f")
        src = (j.get("sources") or [{}])[0].get("type")
        print(f"{cid} route={j['route']} src={src} | {q[:38]} -> {j['answer'][:55]}")


if __name__ == "__main__":
    main()
