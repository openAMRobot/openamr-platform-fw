#!/usr/bin/env bash
# clang-format check for the portable C/C++ core under firmware/ (style: firmware/.clang-format).
# Usage: tools/ci/check_format.sh [DIR]   (default firmware)
# Only DIR is checked: the Teensy overlay, CubeMX-generated code and vendored Drivers/ are never
# reformatted. No C/C++ file in DIR is reported as NOT RUN, never PASS. Exit 1 on a difference.
set -euo pipefail
dir="${1:-firmware}"
CLANG_FORMAT="${CLANG_FORMAT:-clang-format}"
SUMMARY="${GITHUB_STEP_SUMMARY:-/dev/null}"
style="$dir/.clang-format"
[[ -f "$style" ]] || { echo "::error::$style is missing"; exit 1; }
mapfile -t files < <(find "$dir" -type f \( -name '*.c' -o -name '*.h' -o -name '*.cpp' -o -name '*.hpp' \) | LC_ALL=C sort)
version="$("$CLANG_FORMAT" --version)"
echo "$version"
if (( ${#files[@]} == 0 )); then
  echo "::warning title=clang-format::NOT RUN: no C/C++ files under $dir/"
  echo "- clang-format ($version): NOT RUN: no C/C++ files under $dir/" >> "$SUMMARY"
  exit 0
fi
if "$CLANG_FORMAT" --style="file:$style" --dry-run --Werror "${files[@]}"; then
  echo "- clang-format ($version): PASS, ${#files[@]} files under $dir/" >> "$SUMMARY"
else
  echo "::error title=clang-format::files under $dir/ differ from $style; run clang-format --style=file:$style -i"
  echo "- clang-format ($version): FAIL under $dir/" >> "$SUMMARY"
  exit 1
fi
