"""E2 variance experiment: 6 questions x 4 configs x 5 runs -> test_results/variance.md.

The server is restarted for each config with DUMY_TEMP / DUMY_TOPK set, because
sampling is not in config.json. Writes the report after every run so a poll can
watch progress. Run: py tests/variance.py
"""
import json
import os
import statistics
import subprocess
import sys
import time
import urllib.request

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "build", "Release", "dumy.exe")
BASE = "http://127.0.0.1:8085"
OUT = os.path.join(ROOT, "test_results", "variance.md")
REFUSAL = "Bu sual üzrə cavab yarada bilmədim"

QUESTIONS = [
    "İnək neçə ay boğaz qalır?",
    "Sual: inək neçə ay boğaz qalır?",
    "Gündəlik süd verimini necə artırmaq olar?",
    "Qoyunlarda qarın şişkinliyi niyə olur?",
    "Toyuqlarda yumurta verimini nə artırır?",
    "İnəyin südü nə vaxt azalır?",
]

CONFIGS = [
    ("baseline", {"DUMY_TEMP": "0.25", "DUMY_TOPK": "6"}),
    ("temp0.1", {"DUMY_TEMP": "0.1", "DUMY_TOPK": "6"}),
    ("temp0", {"DUMY_TEMP": "0", "DUMY_TOPK": "6"}),
    ("top3", {"DUMY_TEMP": "0.25", "DUMY_TOPK": "3"}),
]
N = 5


def wait_ready(timeout=60):
    end = time.time() + timeout
    while time.time() < end:
        try:
            with urllib.request.urlopen(BASE + "/health", timeout=2) as r:
                if json.loads(r.read()).get("warmup", {}).get("main_ready"):
                    return True
        except Exception:
            pass
        time.sleep(1)
    return False


def start_server(env_extra):
    env = dict(os.environ)
    env.update(env_extra)
    env["DUMY_DEBUG_TRACE"] = "1"
    log = open(os.path.join(ROOT, "data", "variance_server.log"), "a", encoding="utf-8")
    p = subprocess.Popen([EXE, "--no-browser", "--no-web"], cwd=ROOT, env=env,
                         stdout=log, stderr=subprocess.STDOUT,
                         creationflags=subprocess.CREATE_NEW_PROCESS_GROUP)
    if not wait_ready():
        raise RuntimeError("server did not become ready")
    return p


def stop_server(p):
    try:
        subprocess.run(["taskkill", "/PID", str(p.pid), "/F"], capture_output=True)
    except Exception:
        pass
    time.sleep(2)


def ask(q):
    body = json.dumps({"message": q, "session_id": "var", "debug": True}).encode()
    req = urllib.request.Request(BASE + "/chat", data=body,
                                 headers={"Content-Type": "application/json"})
    t0 = time.time()
    with urllib.request.urlopen(req, timeout=180) as r:
        j = json.loads(r.read())
    dt = time.time() - t0
    reasons = []
    for a in j.get("trace", {}).get("attempts", []):
        if a.get("verdict") == "rejected":
            reasons.append((a.get("reason") or "other").strip() or "other")
    refused = j["answer"].startswith(REFUSAL)
    return refused, dt, reasons, j["answer"][:120]


def main():
    results = {}
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("# E2 variance experiment\n\n")

    for name, env_extra in CONFIGS:
        p = start_server(env_extra)
        try:
            rows = []
            for q in QUESTIONS:
                for i in range(N):
                    try:
                        refused, dt, reasons, txt = ask(q)
                    except Exception as e:  # a failed request counts as a refusal
                        refused, dt, reasons, txt = True, 0.0, [f"error: {e}"], ""
                    rows.append({"q": q, "run": i + 1, "refused": refused, "s": dt,
                                 "reasons": reasons, "text": txt})
                    results.setdefault(name, rows)
                    write_report(results, partial=name)
                    print(f"{name} {i+1}/{N} {dt:.1f}s refused={refused}", flush=True)
        finally:
            stop_server(p)
    write_report(results, partial=None)
    print("done", flush=True)


def write_report(results, partial):
    lines = ["# E2 variance experiment", "",
             "6 questions x 5 runs per config; the server was restarted for each config.",
             f"Questions: {json.dumps(QUESTIONS, ensure_ascii=False)}", ""]
    for name, _ in CONFIGS:
        rows = results.get(name)
        if not rows:
            continue
        n = len(rows)
        ref = sum(1 for r in rows if r["refused"])
        lat = [r["s"] for r in rows if r["s"] > 0]
        hist = {}
        for r in rows:
            for why in r["reasons"] or ["none"]:
                hist[why] = hist.get(why, 0) + 1
        lines.append(f"## {name} (n={n})")
        lines.append("")
        lines.append(f"- answered {n - ref}/{n} ({(n - ref) * 100.0 / n:.0f}%), "
                     f"refused {ref}/{n} ({ref * 100.0 / n:.0f}%)")
        if lat:
            lines.append(f"- latency median {statistics.median(lat):.1f}s, max {max(lat):.1f}s")
        lines.append(f"- reject reasons: {json.dumps(hist, ensure_ascii=False)}")
        lines.append("")
        lines.append("| question | run | refused | s | reasons |")
        lines.append("|---|---|---|---|---|")
        for r in rows:
            lines.append(f"| {r['q']} | {r['run']} | {r['refused']} | {r['s']:.1f} | "
                         f"{', '.join(r['reasons']) or '-'} |")
        lines.append("")
    if partial:
        lines.append(f"_partial: {partial} in progress_")
    tmp = OUT + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    os.replace(tmp, OUT)


if __name__ == "__main__":
    main()
