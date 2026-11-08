#!/usr/bin/env python3
"""Step 8: benchmark draft models on the same 10 acceptance questions.

Runs the real DUMY server (full pipeline, bge-m3 on CPU, one main model at a
time), posts every question to /chat, and prints per-question wall time,
generation speed (tok/s) and pass/fail.

Usage:
    python scripts/benchmark_models.py
    python scripts/benchmark_models.py --models qwen2.5:7b qwen3:4b-instruct-2507-q4_K_M
    python scripts/benchmark_models.py --limit 3 --port 8091

No third-party packages: stdlib urllib only. Azerbaijani text is sent as
UTF-8 JSON by Python, so it is not affected by the PowerShell curl alias.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import signal
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EXE = ROOT / "build" / "Release" / "dumy.exe"
OLLAMA = "http://localhost:11434"

DEFAULT_MODELS = ["qwen2.5:7b", "qwen3:4b-instruct-2507-q4_K_M"]

# (question, keywords that must appear, case-insensitive)
QUESTIONS = [
    ("Qoyunların xəstəlikləri haqqında yaz", ["qoyun"]),
    ("Heyvanların bütün xəstəlikləri haqqında yaz", ["xəstəlik"]),
    ("Süd sağma aparatı nədir?", ["sağma", "sağmal", "aparat"]),
    ("Telefon nədir?", ["telefon"]),
    ("Sənin adın nədir?", ["dumy"]),
    ("2 + 2 neçə edir?", ["4"]),
    ("Bu gün hansı gündür?", ["bazar", "çərşənbə", "cümə", "şənbə"]),
    ("Qoyunlarda qarın şişməsi niyə olur?", ["qoyun", "şiş"]),
    ("Arılar üçün yemlik necə hazırlanır?", ["arı", "arï", "yem"]),
    ("Mənə Python kodu yaz", ["kod"]),
]


def http_json(url: str, payload: dict | None = None, timeout: int = 600):
    data = None
    headers = {}
    if payload is not None:
        data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        headers["Content-Type"] = "application/json; charset=utf-8"
    req = urllib.request.Request(url, data=data, headers=headers)
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return json.loads(resp.read().decode("utf-8"))


def wait_health(port: int, timeout: float = 420.0) -> dict:
    deadline = time.time() + timeout
    last = ""
    while time.time() < deadline:
        try:
            h = http_json(f"http://127.0.0.1:{port}/health", timeout=5)
            if h.get("ollama"):
                return h
        except Exception as exc:  # server still starting
            last = str(exc)
        time.sleep(1.5)
    raise RuntimeError(f"server on port {port} never became healthy: {last}")


def installed_models() -> set[str]:
    try:
        tags = http_json(f"{OLLAMA}/api/tags", timeout=10)
        return {m.get("name", "") for m in tags.get("models", [])}
    except Exception:
        return set()


def ollama_pull(model: str) -> bool:
    print(f"  pulling {model} ...", flush=True)
    try:
        req = urllib.request.Request(
            f"{OLLAMA}/api/pull",
            data=json.dumps({"model": model, "stream": False}).encode(),
            headers={"Content-Type": "application/json"},
        )
        with urllib.request.urlopen(req, timeout=3600) as resp:
            resp.read()
        return True
    except Exception as exc:
        print(f"  pull failed: {exc}", flush=True)
        return False


DRAFT_RE = re.compile(
    r"\[timing\] draft model=(?P<model>\S+) wall=(?P<wall>\d+)ms load=(?P<load>\d+)ms "
    r"prompt_eval=(?P<pe>\d+)ms gen=(?P<gen>\d+)ms tok=(?P<ptok>\d+)\+(?P<etok>\d+)"
)


def last_draft_timing(log_path: Path, offset: int = 0) -> tuple[float, int, int]:
    """Returns (gen_ms, eval_tokens, load_ms) of the last draft call.

    Only bytes written for THIS request are read, so an instant fast-path
    answer (identity/arithmetic) does not inherit tok/s from the previous one.
    """
    try:
        with open(log_path, "r", encoding="utf-8", errors="replace") as f:
            f.seek(offset)
            text = f.read()
    except OSError:
        return 0.0, 0, 0
    matches = list(DRAFT_RE.finditer(text))
    if not matches:
        return 0.0, 0, 0
    m = matches[-1]
    return float(m["gen"]), int(m["etok"]), int(m["load"])


def start_server(model: str, port: int, log_path: Path) -> subprocess.Popen:
    log = open(log_path, "w", encoding="utf-8", errors="replace")
    env = dict(os.environ)
    env["PYTHONIOENCODING"] = "utf-8"
    proc = subprocess.Popen(
        [str(EXE), "--port", str(port), "--no-browser", "--no-web",
         "--chat-model", model,
         # One main model for every role: otherwise expansion/rerank/self-check
         # pull qwen2.5:7b from config and Ollama swaps models every call.
         "--helper-model", model, "--selfcheck-model", model],
        cwd=str(ROOT),
        stdout=log,
        stderr=subprocess.STDOUT,
        env=env,
    )
    proc._dumy_log = log  # keep the handle alive
    return proc


def stop_server(proc: subprocess.Popen) -> None:
    try:
        if os.name == "nt":
            proc.terminate()
        else:
            proc.send_signal(signal.SIGTERM)
        proc.wait(timeout=15)
    except Exception:
        try:
            proc.kill()
        except Exception:
            pass


def run_model(model: str, port: int, limit: int, pull: bool) -> list[dict]:
    rows: list[dict] = []
    if model not in installed_models():
        if not pull:
            print(f"  SKIP {model}: not installed (use --pull to download)", flush=True)
            return rows
        if not ollama_pull(model):
            print(f"  SKIP {model}: pull failed", flush=True)
            return rows

    log_path = ROOT / "benchmark" / f"bench_{model.replace(':', '_').replace('/', '_')}.log"
    log_path.parent.mkdir(exist_ok=True)
    print(f"\n=== {model} (port {port}) ===", flush=True)
    proc = start_server(model, port, log_path)
    try:
        health = wait_health(port)
        warm = health.get("warmup") or {}
        loaded = [m.get("name", "") for m in health.get("loaded_models", [])]
        print(f"  warm-up: draft_ready={warm.get('main_ready')} "
              f"resident={warm.get('main_resident')} embed_cpu={warm.get('embed_on_cpu')} "
              f"load_ms={warm.get('main_load_ms')}", flush=True)
        print(f"  single main model: chat=helper=selfcheck={model}", flush=True)
        print(f"  loaded models: {', '.join(loaded) or '-'}", flush=True)

        for i, (question, keywords) in enumerate(QUESTIONS[:limit], 1):
            before = log_path.stat().st_size if log_path.exists() else 0
            t0 = time.time()
            try:
                r = http_json(
                    f"http://127.0.0.1:{port}/chat",
                    {"session_id": f"bench-{model}-{i}", "message": question},
                    timeout=600,
                )
                answer = r.get("answer", "") or ""
                err = r.get("warning", "") if not answer else ""
            except Exception as exc:
                answer, err = "", str(exc)
            wall = time.time() - t0

            gen_ms, etok, load_ms = last_draft_timing(log_path, before)
            tok_s = (etok / (gen_ms / 1000.0)) if gen_ms > 0 else 0.0
            low = answer.lower()
            # Keywords are alternatives (a weekday answer contains ONE of
            # bazar/çərşənbə/cümə/şənbə), never all of them.
            ok = (not err and len(answer.strip()) >= 3
                  and any(k.lower() in low for k in keywords))
            rows.append({"model": model, "n": i, "q": question, "sec": wall,
                         "tok_s": tok_s, "load_ms": load_ms, "ok": ok,
                         "answer": answer, "err": err})
            print(f"  {i:2d}. {wall:5.1f}s  {tok_s:5.1f} tok/s  "
                  f"load={load_ms:5d}ms  {'PASS' if ok else 'FAIL'}  {question}",
                  flush=True)
            if not ok:
                if err:
                    print(f"      -> error: {err[:140]}", flush=True)
                elif answer:
                    print(f"      -> {answer[:140].replace(chr(10), ' ')}", flush=True)
    finally:
        stop_server(proc)
    return rows


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--models", nargs="+", default=DEFAULT_MODELS)
    ap.add_argument("--port", type=int, default=8091)
    ap.add_argument("--limit", type=int, default=len(QUESTIONS))
    ap.add_argument("--pull", action="store_true", help="pull a missing model")
    args = ap.parse_args()

    if not EXE.exists():
        print(f"missing {EXE} - build first", file=sys.stderr)
        return 1

    all_rows: list[dict] = []
    for model in args.models:
        all_rows.extend(run_model(model, args.port, max(1, args.limit), args.pull))

    print("\n=== SUMMARY ===")
    print(f"{'model':34s} {'q':>2s} {'sec':>7s} {'tok/s':>7s} {'load':>7s}  result")
    for r in all_rows:
        print(f"{r['model']:34s} {r['n']:2d} {r['sec']:7.1f} {r['tok_s']:7.1f} "
              f"{r['load_ms']:7d}  {'PASS' if r['ok'] else 'FAIL'}")
    for model in args.models:
        rows = [r for r in all_rows if r["model"] == model]
        if not rows:
            continue
        passed = sum(1 for r in rows if r["ok"])
        avg = sum(r["sec"] for r in rows) / len(rows)
        print(f"{model}: {passed}/{len(rows)} passed, avg {avg:.1f}s/question")
    return 0


if __name__ == "__main__":
    # Windows consoles default to a local codepage (cp1254 here) that cannot
    # encode Azerbaijani letters; force UTF-8 so printing never aborts a run.
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")
    sys.exit(main())
