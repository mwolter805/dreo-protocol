#!/bin/sh
set -eu

mode="${1:-fixed}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build="$root/tests/host/.build"
mkdir -p "$build"

# The Python legs need Python 3.10 or newer. Pick the interpreter up front so a
# shell whose bare `python3` is an older system Python fails here, with a
# message that names the problem, rather than inside the decoder after the C++
# legs have already run. PYTHON=/path/to/python3 names the interpreter
# explicitly and is used as given or refused; otherwise the first of
# /opt/homebrew/bin/python3 and python3 on PATH that is new enough is used.
python_ok() {
  [ -x "$1" ] && "$1" -c 'import sys; sys.exit(0 if sys.version_info >= (3, 10) else 1)' 2>/dev/null
}
python=""
if [ -n "${PYTHON:-}" ]; then
  if python_ok "$PYTHON"; then
    python="$PYTHON"
  else
    echo "run_tests.sh: PYTHON='$PYTHON' is not an executable Python 3.10+; the Python legs need 3.10 or newer" >&2
    exit 1
  fi
else
  for candidate in /opt/homebrew/bin/python3 "$(command -v python3 2>/dev/null || true)"; do
    [ -n "$candidate" ] || continue
    if python_ok "$candidate"; then
      python="$candidate"
      break
    fi
  done
  if [ -z "$python" ]; then
    echo "run_tests.sh: no Python 3.10+ found (tried /opt/homebrew/bin/python3 and python3 on PATH); set PYTHON=/path/to/python3" >&2
    exit 1
  fi
fi

cxxflags="-std=c++17 -Wall -Wextra -fsanitize=address,undefined -fno-omit-frame-pointer"
if [ "$mode" = fixed ]; then
  cxxflags="$cxxflags -DDREO_FIXED_TESTS"
fi

# shellcheck disable=SC2086
c++ $cxxflags \
  -I "$root/tests/host/stubs" \
  -I "$root" \
  "$root/components/dreo/dreo.cpp" \
  "$root/components/dreo/fan/dreo_fan.cpp" \
  "$root/components/dreo/light/dreo_light.cpp" \
  "$root/components/dreo/lock/dreo_lock.cpp" \
  "$root/components/dreo/number/dreo_number.cpp" \
  "$root/components/dreo/select/dreo_select.cpp" \
  "$root/components/dreo/switch/dreo_switch.cpp" \
  "$root/components/dreo/binary_sensor/dreo_binary_sensor.cpp" \
  "$root/components/dreo/text/dreo_text.cpp" \
  "$root/components/dreo_ceiling_fan/dreo_ceiling_fan.cpp" \
  "$root/components/dreo_ceiling_fan/fan/dreo_ceiling_fan_fan.cpp" \
  "$root/components/dreo_ceiling_fan/light/dreo_ceiling_fan_light.cpp" \
  "$root/tests/host/dreo_host_test.cpp" \
  -o "$build/dreo_host_test"

cd "$root"
"$build/dreo_host_test" "$mode"

if [ "$mode" = fixed ]; then
  PYTHONDONTWRITEBYTECODE=1 "$python" tests/host/test_decode.py --fixed
  PYTHONDONTWRITEBYTECODE=1 "$python" tests/host/test_phase1_contract.py
else
  PYTHONDONTWRITEBYTECODE=1 "$python" tests/host/test_decode.py
fi
