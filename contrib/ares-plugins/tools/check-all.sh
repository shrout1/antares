#!/bin/bash
# Load-check and smoke-play every converted plugin (part B of verify/run-checks.sh).
#   check-all.sh ANTARES_SRC VERIFY_DIR CONVERTED_DIR WORK_DIR [RESULTS_FILE]
# Prints one line per plugin: "PASS name ..." or "FAIL name: reason". With RESULTS_FILE, lines
# are appended there too and plugins already listed in it are skipped, so a run can resume.
set -u
ANTARES=$1; HERE=$2; CONV=$3; WORK=$4; RESULTS=${5:-/dev/null}
mkdir -p "$WORK"
report() { echo "$1"; [ "$RESULTS" != /dev/null ] && echo "$1" >> "$RESULTS"; }
for PLUGIN in "$CONV"/*.antaresplugin; do
  n=$(basename "$PLUGIN" .antaresplugin)
  # exact name only: "PASS X (Y) (12 objects" must not count as a result for "X"
  if [ -f "$RESULTS" ] && awk -v n="$n" '
      index($0, "PASS " n " (") == 1 && substr($0, length("PASS " n " (") + 1, 1) ~ /[0-9]/ { f = 1 }
      index($0, "FAIL " n ":") == 1 { f = 1 }
      END { exit !f }' "$RESULTS"; then continue; fi
  W="$WORK/$n"; rm -rf "$W"; mkdir -p "$W/p"
  (cd "$W/p" && unzip -q "$PLUGIN")
  (cd "$W/p/objects" && find . -name '*.pn' | sed 's#^\./##; s#\.pn$##') > "$W/objects.txt"
  chapters=$(grep -h '^chapter' "$W"/p/levels/*.pn 2>/dev/null | awk '{print $2}' | sort -n)
  # net-only plugins have no solo chapters; load-check them through Ares chapter 1
  first=$(echo "$chapters" | head -1); first=${first:-1}
  python3 -c "import sys; sys.path.insert(0,'$HERE'); import nlrp; nlrp.write('$W/first.NLRP', $first)"
  if ! lc=$(cd "$ANTARES" && timeout 300 env LOADCHECK="$W/objects.txt" ./out/cur/replay "$W/first.NLRP" --text -o "$W/lc" --plugin "$PLUGIN" 2>&1); then
    report "FAIL $n: loadcheck: $(echo "$lc" | grep -v '^loadcheck' | head -3 | tr '\n' ' ' | cut -c1-300)"; continue
  fi
  bad=""; nch=0
  for ch in $chapters; do
    nch=$((nch+1))
    python3 -c "import sys; sys.path.insert(0,'$HERE'); import nlrp; nlrp.write('$W/c.NLRP', $ch, duration=7200)"
    if ! (cd "$ANTARES" && timeout 600 ./out/cur/replay "$W/c.NLRP" --text -o "$W/c$ch" --plugin "$PLUGIN") >"$W/c$ch.log" 2>&1; then
      bad="$bad ch$ch($(head -c 150 "$W/c$ch.log" | tr '\n' ' '))"
    fi
    rm -rf "$W/c$ch"
  done
  if [ -n "$bad" ]; then report "FAIL $n: chapters:$bad"
  elif [ $nch = 0 ]; then report "PASS $n ($(wc -l < "$W/objects.txt") objects, net-only: load check only)"
  else report "PASS $n ($(wc -l < "$W/objects.txt") objects, $nch chapters)"; fi
  rm -rf "$W"
done
echo "DONE"
