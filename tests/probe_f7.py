"""F7 probe: reachability of curated entries and unchanged routes."""
import json
import sys
import time
import urllib.request

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
BASE = "http://127.0.0.1:8085"


def ask(q, sid=""):
    body = json.dumps({"message": q, "session_id": "p7-" + sid, "debug": True}).encode()
    req = urllib.request.Request(BASE + "/chat", data=body,
                                 headers={"Content-Type": "application/json"})
    t = time.time()
    j = json.loads(urllib.request.urlopen(req, timeout=150).read())
    return time.time() - t, j


REACH = [
    "Pişik niyə xırıldayır?", "pisik niye xirildayir?",
    "İt quyruğunu niyə bulayır?", "İt niyə quyruğunu tərpədir?",
    "Ördəklər niyə islanmır?", "Ördək niyə suda islanmır?",
    "Keçinin gözü niyə belədir?", "Keçinin bəbəyi niyə üfüqidir?",
    "Bal niyə xarab olmur?", "Bal niyə uzun müddət saxlanılır?",
    "Xoruz niyə səhər banlayır?", "Arılar necə danışır?", "İnək niyə gövşəyir?",
]


def main():
    print("== reachability")
    hit = 0
    for i, q in enumerate(REACH):
        dt, j = ask(q, str(i))
        src = (j.get("sources") or [{}])[0]
        kid = src.get("id") or ""
        ok = src.get("type") == "knowledge"
        hit += ok
        print(f"{dt:.2f}s {j['route']:<12} {src.get('type')} {kid} | {q[:34]}")
    print(f"knowledge hits: {hit}/{len(REACH)}")

    print("== former cases")
    cases = json.load(open("tests/expected/cases.json", encoding="utf-8-sig"))["cases"]
    by = {c["id"]: c["question"] for c in cases}
    for cid in ["t10_capital", "t16_pdf_diagnosis"]:
        q = by.get(cid)
        dt, j = ask(q, cid)
        src = (j.get("sources") or [{}])[0].get("type")
        print(f"{cid}: route={j['route']} src={src} arb={(j.get('trace') or {}).get('arbitration')} | {q[:36]}")


if __name__ == "__main__":
    main()
