#!/bin/zsh
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build/macos-llvm-cov}"
DIST_DIR="${DIST_DIR:-$ROOT_DIR/dist}"
TEST_BIN="$BUILD_DIR/cupuacu-tests"
PROFRAW="$DIST_DIR/cupuacu-tests.profraw"
PROFDATA="$DIST_DIR/cupuacu-tests.profdata"
LLVM_COV_BIN="${LLVM_COV_BIN:-$(xcrun --find llvm-cov)}"
LLVM_PROFDATA_BIN="${LLVM_PROFDATA_BIN:-$(xcrun --find llvm-profdata)}"

mkdir -p "$DIST_DIR"

if [[ ! -f "$BUILD_DIR/CMakeCache.txt" || "${FORCE_CONFIGURE:-0}" == "1" ]]; then
  cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_FLAGS="-fprofile-instr-generate -fcoverage-mapping" \
    -DCMAKE_CXX_FLAGS="-fprofile-instr-generate -fcoverage-mapping" \
    -DCMAKE_EXE_LINKER_FLAGS="-fprofile-instr-generate" \
    -DCMAKE_SHARED_LINKER_FLAGS="-fprofile-instr-generate"
fi

if [[ "${SKIP_BUILD:-0}" != "1" ]]; then
  cmake --build "$BUILD_DIR" --target cupuacu-tests -j4
fi

LLVM_PROFILE_FILE="$PROFRAW" "$TEST_BIN"

"$LLVM_PROFDATA_BIN" merge -sparse "$PROFRAW" -o "$PROFDATA"
"$LLVM_COV_BIN" report "$TEST_BIN" -instr-profile="$PROFDATA" "$ROOT_DIR/src/main"

print_module_summary() {
  local label="$1"
  shift
  local total_line
  total_line="$("$LLVM_COV_BIN" report "$TEST_BIN" -instr-profile="$PROFDATA" "$@" | awk '/^TOTAL[[:space:]]/{print $0}')"
  if [[ -z "$total_line" ]]; then
    return
  fi

  local -a fields
  fields=(${=total_line})
  local covered_lines=$(( fields[8] - fields[9] ))
  printf "%-28s %8s/%-8s %8s\n" \
    "$label" \
    "${covered_lines}" \
    "${fields[8]}" \
    "${fields[10]}"
}

echo
echo "By Module (line coverage)"
printf "%-28s %8s  %8s\n" "Module" "Covered/Total" "Coverage"

root_files=("$ROOT_DIR"/src/main/*(.N))
if (( ${#root_files[@]} > 0 )); then
  print_module_summary "src/main" "${root_files[@]}"
fi

for module_dir in "$ROOT_DIR"/src/main/*(/N); do
  print_module_summary "src/main/${module_dir:t}" "$module_dir"
done
