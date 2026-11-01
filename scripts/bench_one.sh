#!/usr/bin/env bash
# Benchmark ONE draft model on the 5 fixed Azerbaijani questions.
# Usage:  bash scripts/bench_one.sh qwen2.5:7b
# Produces: benchmark/<model>_<question>.json  +  benchmark/timings.csv
# The server is started with --no-browser, one fresh session per question.
set -u
cd "$(dirname "$0")/.."
EXE="build/Release/dumy.exe"
PORT=8085
MODEL="${1:-qwen2.5:7b}"
OUT=benchmark
mkdir -p "$OUT"
MFILE="${MODEL//[:\/]/_}"
CSV="$OUT/timings.csv"
[ -f "$CSV" ] || echo "model,question,seconds,http" > "$CSV"

QUESTIONS=(
  "Qoyunların xəstəlikləri haqqında yaz"
  "Heyvanların bütün xəstəlikləri haqqında yaz"
  "Süd sağma aparatı nədir?"
  "Mənə Python kodu yaz"
  "Qoyunlarda diareyanın səbəbləri və müalicəsi nədir?"
)
NAMES=(q1_qoyun_xestelikleri q2_butun_xestelikler q3_sagma_aparati q4_python_kod q5_diareya)

# stop any running instance (never two servers on one data/ folder)
taskkill //IM dumy.exe //F >/dev/null 2>&1 || true
sleep 1

echo "### BENCH MODEL=$MODEL starting $(date +%H:%M:%S)"
"$EXE" --no-browser --chat-model "$MODEL" > "$OUT/server_${MFILE}.log" 2>&1 &
SRV=$!
for i in $(seq 1 90); do
  curl -s -m 2 "http://127.0.0.1:$PORT/health" >/dev/null 2>&1 && break
  sleep 1
done
if ! curl -s -m 2 "http://127.0.0.1:$PORT/health" >/dev/null 2>&1; then
  echo "### BENCH MODEL=$MODEL SERVER FAILED TO START"
  exit 1
fi
echo "### server up (pid $SRV), model=$MODEL"
ollama ps 2>/dev/null | tail -2

for i in "${!QUESTIONS[@]}"; do
  Q="${QUESTIONS[$i]}"
  N="${NAMES[$i]}"
  printf '{"session_id":"bench-%s","message":"%s"}' "$N" "$Q" > "$OUT/q.json"
  S=$(date +%s)
  CODE=$(curl -s -m 600 -X POST "http://127.0.0.1:$PORT/chat" \
    -H "Content-Type: application/json; charset=utf-8" \
    --data-binary "@$OUT/q.json" \
    -o "$OUT/${MFILE}_${N}.json" -w "%{http_code}")
  E=$(date +%s)
  echo "$MODEL,$N,$((E-S)),$CODE" >> "$CSV"
  echo "### $MODEL $N -> ${CODE} in $((E-S))s"
  if [ "$i" = "0" ]; then ollama ps 2>/dev/null | tail -2; fi
done

taskkill //IM dumy.exe //F >/dev/null 2>&1 || true
echo "### BENCH MODEL=$MODEL done $(date +%H:%M:%S)"
