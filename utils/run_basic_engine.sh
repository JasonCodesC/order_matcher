#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT_DIR"

g++ -O3 -g -Wall -Wextra -std=c++17 -march=native -pthread \
  -Isrc/cpp -Isrc -Isrc/cpp_helpers \
  src/basic_cpp/basic_engine.cpp src/cpp/match.cpp -o basic_engine

./basic_engine
