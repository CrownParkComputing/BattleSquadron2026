#!/bin/sh
# Parity smoke: run the native engine against the oracle captures and require
#  * scroll + player exact over the whole aligned range (fire + invuln runs)
#  * every hostile spawn in lockstep (time, slot, type, x)
#  * per-type live counts identical in the hostile and object pools
# (Full-capture record-level parity status: see PROJECT.md session-2 entry.)
set -e
cd "$(dirname "$0")/.."
FIRE=re/trace/objlog_fire_30000.txt
INVULN=re/trace/objlog_invuln_20000.txt
[ -f "$FIRE" ] || { echo "parity smoke: no captures (re/trace); skipping"; exit 0; }

./build/simrun 0 14300 build/native_fire.txt --fire --autopilot --invuln --fbase 1389 >/dev/null
./build/simrun 0 9200 build/native_invuln.txt --autopilot --invuln --fbase 1481 >/dev/null
./build/simrun 0 4000 build/native_demo.txt --demo >/dev/null

python3 - <<'EOF'
import subprocess, sys
fails = 0
for host, native, frames in (("re/trace/objlog_fire_30000.txt", "build/native_fire.txt", 14300),
                             ("re/trace/objlog_invuln_20000.txt", "build/native_invuln.txt", 9200),
                             ("re/trace/objlog_demo_30000.txt", "build/native_demo.txt", 4000)):
    out = subprocess.run(["python3", "tools/parity.py", host, native],
                         capture_output=True, text=True).stdout
    ok = True
    need = ["G  (cam/progress/7222/7212): %d/%d match" % (frames, frames),
            "P0 (x,y,b38,b39,b48,b49,w52,b56,w60,w68): %d/%d match" % (frames, frames),
            "H pool: live counts per type match",
            "O pool: live counts per type match"]
    for n in need:
        if n not in out: ok = False
    if "in lockstep" in out:
        # "H spawns: host N native N, first N in lockstep"
        import re
        m = re.search(r"H spawns: host (\d+) native (\d+), first (\d+) in lockstep", out)
        if not m or m.group(1) != m.group(2) or m.group(2) != m.group(3): ok = False
    print(("PASS " if ok else "FAIL ") + host)
    if not ok:
        print(out)
        fails += 1

# the demo replay is byte-exact end to end: require ALL pools identical per frame
sys.path.insert(0, "tools")
from parity import load, align
pairs = align(load("re/trace/objlog_demo_30000.txt"), load("build/native_demo.txt"))
for pool in ("G", "P", "S", "H", "O", "E"):
    bad = sum(1 for h, n in pairs if h[pool] != n[pool])
    tag = "PASS" if bad == 0 else "FAIL"
    print("%s demo %s pool byte-identical %d/%d" % (tag, pool, len(pairs) - bad, len(pairs)))
    if bad: fails += 1
sys.exit(1 if fails else 0)
EOF
echo "test_parity_smoke.sh: PASS"
