#!/bin/bash
# Builds and runs the SceneObject validation on Linux with g++, then records the output in Results/.
# Prerequisites: g++ with C++17. No ImGui or toml++ needed; the inspector check uses ControlKit headers only.
set -e
EXHIBIT="$(cd "$(dirname "$0")/.." && pwd)"
ROOT="$(cd "$EXHIBIT/../.." && pwd)"
# Binary goes to the workspace scratch folder (git-ignored), not /tmp.
BUILD="$ROOT/_ClaudeScratch/SceneObjectValidation-build"
mkdir -p "$BUILD" "$EXHIBIT/Results"

g++ -std=c++17 -Wall -Wextra -Werror -I"$ROOT/Engine/SceneObject" \
    "$EXHIBIT/Code/SceneObjectValidation.cpp" "$ROOT/Engine/SceneObject/"*.cpp -o "$BUILD/SceneObjectValidation"

g++ -std=c++17 -Wall -Wextra -fsyntax-only -I"$ROOT/Engine/Interface" -I"$ROOT/Engine/SceneObject" \
    "$ROOT/UI/Editor/SceneObjectInspector.cpp"
echo "Inspector syntax check: PASS (UI/Editor/SceneObjectInspector.cpp against ControlKit headers)"

"$BUILD/SceneObjectValidation" | tee "$EXHIBIT/Results/SceneObjectValidation-results.txt"
