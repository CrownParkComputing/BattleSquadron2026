# Amiga WHDLoad title → native: the cookbook (as run on SWIV, 2026-08-19)

Stages live in `~/BattleSquadron-Amiga/tools/recomp_studio.py` (+ tools/), per-game JSON.
Worked example: `~/SWIV-Native` (swiv_project.json, re/, src/engine, src/behaviours, tools/parity.py).

1. INGEST. Extract the WHDLoad install; fetch the official whdload.de package (may ship `source/`).
   Disk-image installs (Disk.1 = RawDIC dump, no filesystem) need the game's own loader layout:
   either the slave source documents it, or go DYNAMIC: boot the slave in the Musashi host
   (`~/SWIV-Amiga/build/swiv` 512K chip / `build/swiv2mb` 2MB chip: `--dir INSTALL --slave X.slave
   --frames N --ppm-seq P --ppm-every K --dump-file BASE LEN FILE (repeatable) --objlog F --copper F`,
   env SWIV_STATELOG/SWIV_STATELOG_FROM/SWIV_STATELOG_MAX (register trace), SWIV_PCSET (executed PCs),
   SWIV_TRACE_COP1 (COP1LC writes)). The host honours ws_CurrentDir. --no-video stalls some games.
2. ORACLE. Screenshots every N frames + RAM dumps + register trace + PC set. Check the game actually
   runs (blits/pixels/dmacon in the per-100-frame log). If the display is wrong, the HOST chipset is
   the gap (SWIV host: OCS, 4-5 planes, no HAM/EHB/DPF/sprites) — fix the host or use another oracle.
3. base-detect → seed-disasm (IRA + Ghidra seeded with executed PCs/call targets) → dispatch-table
   (kernel family pattern) → objwalk (task lists + stack unwind) → objlog-stats (activation thresholds).
   Families: `salescurve` so far; add a preset per engine family (kernel_objwalk.py FAMILIES,
   find_dispatch.py PATTERN).
4. TRANSLATE (route B): one agent decodes the verb library to VERBS.md/OBJECT.md (C pseudocode per
   routine, verified against the register trace); define engine.h (coroutine object model, verbs);
   fan out handler groups to agents with PORTING_GUIDE.md; player/bullets/manager separately.
5. PARITY: `tools/simrun` writes the native objlog in the host's format; `tools/parity.py` aligns by
   scroll/time and diffs per-graphic counts/trajectories. RNG-driven positions match statistically.
6. Assets: formats decoded statically (sprites/maps/palettes) → native decoder in C (swivdata.c),
   identity-tested against the reference renderer. Sound: Paula parameters from the sound routines
   (period/volume/wave) → synthesised; MODs via libxmp; samples as-is.

Lessons from Uridium 2 (2026-08-19):
- Host gaps found: copper SKIP (was a no-op), CIA-B timer B (game tick!), one
  disk image cached for all disk numbers.  All fixed in ~/SWIV-Amiga; "idle loop
  + intreq pending" = an interrupt source the host does not model.
- `SWIV_WATCH=lo-hi` (first read/write per PC in a range) finds the reader of
  any data blob in one run: tile set, map, palette, shape table in minutes.
- `--ptrlog FILE SPEC` is the title-neutral objlog (pointer arrays / linked
  lists / N list heads + globals) -- no host code per title.
- Disk-image installs may carry a real (custom) filesystem: look for directory
  strings (8.3 names) and a FAT; Graftgold = 'K2' FS + PP20 files.
- Data files often nest PP20 chunks whose length lives in a header elsewhere
  (stage headers); brute-force the end by "decrunch consumes all input".
