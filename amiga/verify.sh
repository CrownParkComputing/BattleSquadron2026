#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"

data=original/whdload/BattleSquadron/data
if [ ! -f "$data/LOADER" ]; then
    echo "Missing extracted WHDLoad data; run ./regen_asm.sh first." >&2
    exit 1
fi

# Re-run every BOND stream through the checksum-validating depacker.
tools/extract_modules.py "$data" original/modules docs/module-map.json >/dev/null
mkdir -p build

vasmm68k_mot -Fbin -no-opt -quiet \
    -o build/loader-rebuilt.bin asm/loader.asm
vasmm68k_mot -Fbin -no-opt -quiet \
    -o build/lodgam-rebuilt.bin asm/lodgam.asm
vasmm68k_mot -Fbin -no-opt -quiet \
    -o build/lodcom-rebuilt.bin asm/lodcom.asm

cmp "$data/LOADER" build/loader-rebuilt.bin
cmp original/modules/LODGAM.bin build/lodgam-rebuilt.bin
cmp original/modules/LODCOM.bin build/lodcom-rebuilt.bin

echo "OK: LOADER byte-exact (67584 bytes)"
echo "OK: LODGAM runtime image byte-exact (38108 bytes)"
echo "OK: LODCOM runtime image byte-exact (20218 bytes)"
echo "OK: all 15 BOND streams passed their embedded XOR checksum"

