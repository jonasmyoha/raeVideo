#!/bin/sh
# Runs every test program (tests/<name>/Main.rae with an expected.txt) from
# the repository root and compares its output with expected.txt. Each test
# may have a config.env (KEY=value lines) for its environment. Exit 1 when
# one fails. RAE_ROOT=<a Rae checkout> builds with that Rae instead of the
# pinned one (./rae says so).
set -u
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root" || exit 1
filter=${1:-}
passed=0
failed=0
for dir in tests/*/; do
  name=$(basename "$dir")
  [ -f "$dir/Main.rae" ] && [ -f "$dir/expected.txt" ] || continue
  case "$name" in *"$filter"*) ;; *) continue ;; esac
  environment=""
  [ -f "$dir/config.env" ] && environment=$(grep -v '^#' "$dir/config.env" | tr '\n' ' ')
  output=$(env $environment RAE_FORMAT=check perl -e 'alarm shift; exec @ARGV' 600 \
    ./rae run "$dir/Main.rae" 2>&1 | grep -v '^@@' | grep -v '^note: RAE_ROOT is set')
  if [ "$output" = "$(cat "$dir/expected.txt")" ]; then
    echo "PASS: $name"
    passed=$((passed + 1))
  else
    echo "FAIL: $name"
    printf '%s\n' "$output" | diff - "$dir/expected.txt" | head -20
    failed=$((failed + 1))
  fi
done
# Checks that are scripts, not programs: tools/check<Name>.sh (on macOS)
if [ "$(uname -s)" = Darwin ]; then
  for script in tools/check*.sh; do
    [ -f "$script" ] || continue
    name=$(basename "$script" .sh)
    case "$name" in *"$filter"*) ;; *) continue ;; esac
    if output=$(sh "$script" 2>&1); then
      echo "PASS: $name"
      passed=$((passed + 1))
    else
      echo "FAIL: $name"
      printf '%s\n' "$output" | tail -20
      failed=$((failed + 1))
    fi
  done
fi
echo "Results: $passed passed, $failed failed"
[ "$failed" -eq 0 ]
