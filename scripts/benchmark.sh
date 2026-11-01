#!/usr/bin/env bash
# Runs the 5-question benchmark for every draft-model candidate, one after
# another (they must never share the GPU). Result: benchmark/timings.csv
set -u
cd "$(dirname "$0")/.."
for M in "qwen2.5:7b" "llama3.1:8b" "qwen3:8b"; do
  bash scripts/bench_one.sh "$M"
done
echo "### ALL BENCHMARKS DONE"
cat benchmark/timings.csv
