#!/usr/bin/env python3
"""Amiga Recomp Studio -- drive a WHDLoad game through the pipeline to a
native (no-68000-emulator) port.

A tkinter GUI plus a headless CLI.  Musashi is used only as a development
oracle; the shipping target is the translated C in ``src/recomp/``.  Every
pipeline stage is automated except the 68000 -> C translation itself, which
the app tracks as an "assisted" stage and points you at.

GUI:
    python3 tools/recomp_studio.py

Headless (no display needed):
    python3 tools/recomp_studio.py --list
    python3 tools/recomp_studio.py --stage modules
    python3 tools/recomp_studio.py --project path/to/game.json --stage verify

The project model is a JSON file that records the game's paths, per-stage
configuration, and per-stage completion status, so a run can be resumed and a
new title only needs a new JSON file (the reusable platform layer and the
pipeline logic stay unchanged).
"""

from __future__ import annotations

import argparse
import json
import queue
import shlex
import subprocess
import sys
import threading
from datetime import datetime
from pathlib import Path

try:
    import tkinter as tk
    from tkinter import filedialog, messagebox, scrolledtext, ttk
except ImportError:  # headless machines without tkinter still get --list/--stage
    tk = filedialog = messagebox = scrolledtext = ttk = None

ROOT = Path(__file__).resolve().parent.parent

# ---------------------------------------------------------------------------
# Project model
# ---------------------------------------------------------------------------

# Battle Squadron is the seeded project.  A new title supplies the same keys
# pointing at its own WHDLoad install, and (for the oracle/test stages) its
# own deterministic milestone numbers and recomp Makefile target.
#
# EVERY per-title command lives here.  Nothing in the stage table may fall back
# to a hard-coded Battle Squadron script or make target: running Hybris through
# the pipeline on 2026-08-16 showed four stages reporting "done" while they had
# quietly rebuilt and verified Battle Squadron, including a cheerful
# "OK: LOADER byte-exact (67584 bytes)" for a game that was not being ported.
# A stage a project does not configure must come out "unsupported", never green.
BATTLE_SQUADRON = {
    "name": "Battle Squadron",
    "root": str(ROOT),
    "raw_slave": "original/whdload/BattleSquadron/BattleSquadron.slave",
    "slave_out": "original/slave.bin",
    "data_dir": "original/whdload/BattleSquadron/data",
    "modules_out": "original/modules",
    "manifest": "docs/module-map.json",
    # "loader-bond" is the Cope-com LOADER descriptor table + BOND-packed
    # overlays.  It is NOT a WHDLoad convention -- Hybris, by the same authors,
    # does not use it -- so a new title starts at "none" until proven otherwise.
    "overlay_layout": "loader-bond",
    "discovery": {
        "binary": "original/whdload/BattleSquadron/data/LOADER",
        "base": "0x100",
        "entries": ["0x400", "0x150", "0x1e0", "0x298",
                    "0x1ba8", "0x1c9e", "0xab46", "0xead0"],
        "output": "build/loader.ira",
    },
    "scripts": {
        "assemble": "./regen_asm.sh",
        "verify": "./verify.sh",
    },
    "make_targets": {
        "unit_test": "unit-test",
        "integration_test": "integration-test",
        "recomp_test": "recomp-test",
        "recomp_dump": "build/recomp_dump",
    },
    "oracle": {
        "make_target": "build/battle_squadron_native",
        "binary": "build/battle_squadron_native",
        "frames": "50000",
        "expect_files": "20",
        "expect_blits": "2054047",
    },
    "parity": {
        "native_frames": "6000",
        "game_frames": "600",
    },
    "gate_binary": "build/test_recomp_boot",
    "stages": {},
}


class StageUnsupported(Exception):
    """The project does not configure this stage for its own title.

    Raised instead of silently reusing another game's script, make target or
    binary.  The runner records the stage as "unsupported" and carries on.
    """


def resolve(project: dict, path: str) -> str:
    """Resolve a project path (relative to the project root) to a string."""
    p = Path(path)
    if not p.is_absolute():
        p = Path(project["root"]) / p
    return str(p)


def need(project: dict, *keys, what: str = ""):
    """Fetch a nested project value, or refuse to run the stage without it."""
    node = project
    for key in keys:
        if not isinstance(node, dict) or not node.get(key):
            path = ".".join(keys)
            detail = f" ({what})" if what else ""
            raise StageUnsupported(
                f"{project['name']} does not define '{path}'{detail}. "
                f"Add it to the project JSON; this stage will not fall back "
                f"to another title's.")
        node = node[key]
    return node


def need_input(project: dict, path: str, what: str) -> str:
    """Resolve an input path that must already exist on disk."""
    full = resolve(project, path)
    if not Path(full).exists():
        raise StageUnsupported(f"{what} not found at {full}")
    return full


# ---------------------------------------------------------------------------
# Stage definitions
# ---------------------------------------------------------------------------
# Each stage function returns a list of steps: (label, argv_list).  argv paths
# are resolved against the project root.  ``kind`` is "auto" (runs a command)
# or "assisted" (manual/AI work; the app only records and guides it).


def _ingest(project: dict):
    """Generic: every WHDLoad slave is an AmigaDOS hunk file with the same
    header.  Verified against Hybris as well as Battle Squadron."""
    return [("extract slave code hunk",
             ["python3", "tools/extract_slave.py",
              need_input(project, need(project, "raw_slave"), "slave"),
              resolve(project, need(project, "slave_out"))])]


def _modules(project: dict):
    """Title-specific: tools/extract_modules.py reads a LOADER descriptor table
    and BOND-depacks the overlays it names.  That is the Cope-com LOADER
    layout, not a WHDLoad one, so a title has to declare it."""
    layout = project.get("overlay_layout", "none")
    if layout != "loader-bond":
        raise StageUnsupported(
            f"overlay_layout is '{layout}': there is no LOADER descriptor "
            f"table to depack.  Write a title-specific extractor and set "
            f"overlay_layout once it exists.")
    data_dir = need_input(project, need(project, "data_dir"), "data directory")
    if not (Path(data_dir) / "LOADER").exists():
        raise StageUnsupported(
            f"overlay_layout is 'loader-bond' but {data_dir}/LOADER is missing")
    return [("build runtime overlays + manifest",
             ["python3", "tools/extract_modules.py", data_dir,
              resolve(project, need(project, "modules_out")),
              resolve(project, need(project, "manifest"))])]


def _discover(project: dict):
    """Generic: Capstone recursive descent from a list of entry points."""
    binary = need_input(project, need(project, "discovery", "binary"),
                        "discovery binary")
    argv = ["python3", "tools/discover_code.py", binary,
            "--base", need(project, "discovery", "base"),
            "--output", resolve(project, need(project, "discovery", "output"))]
    for entry in need(project, "discovery", "entries"):
        argv += ["--entry", entry]
    return [("Capstone reachable-code discovery", argv)]


def _assemble(project: dict):
    script = need(project, "scripts", "assemble",
                  what="a script that reassembles THIS title's overlays")
    return [("reassemble overlays (vasmm68k)", [script])]


def _verify(project: dict):
    script = need(project, "scripts", "verify",
                  what="a script that cmp's THIS title's rebuilt overlays")
    return [("byte-exact verification", [script])]


def _oracle(project: dict):
    target = need(project, "oracle", "make_target",
                  what="a Musashi oracle for this title")
    o = project["oracle"]
    return [
        ("build Musashi oracle", ["make", target]),
        ("deterministic milestone",
         [resolve(project, need(project, "oracle", "binary")),
          "--frames", o["frames"],
          "--expect-files", o["expect_files"],
          "--expect-blits", o["expect_blits"]]),
    ]


def _translate(project: dict):
    return []  # assisted stage; handled specially by the runner


def _unit_test(project: dict):
    return [("fast unit tests + native selftest",
             ["make", need(project, "make_targets", "unit_test",
                           what="this title's unit-test make target")])]


def _integration_test(project: dict):
    return [("milestone integration checks",
             ["make", need(project, "make_targets", "integration_test",
                           what="this title's integration make target")])]


def _recomp_test(project: dict):
    return [("recomp test suite + link audit",
             ["make", need(project, "make_targets", "recomp_test",
                           what="this title's recomp-test make target")])]


def _parity(project: dict):
    oracle_target = need(project, "oracle", "make_target",
                         what="a Musashi oracle for this title")
    dump_target = need(project, "make_targets", "recomp_dump",
                       what="this title's recomp state dumper")
    p = project["parity"]
    native = resolve(project, need(project, "oracle", "binary"))
    recomp_dump = resolve(project, dump_target)
    prefix = project.get("build_prefix", "build/parity")
    ref_txt = resolve(project, f"{prefix}_ref.txt")
    ours_txt = resolve(project, f"{prefix}_ours.txt")
    return [
        ("build oracle + recomp dumper",
         ["make", oracle_target, dump_target]),
        ("native reference dump (fire held)",
         [native, "--frames", p["native_frames"], "--fire",
          "--dump-state", "1"], ref_txt),
        ("recomp dump (fire held)",
         [recomp_dump, p["game_frames"]], ours_txt),
        ("diff reference vs recomp",
         ["python3", "tools/parity_diff.py", ref_txt, ours_txt]),
    ]


def _gate(project: dict):
    g = resolve(project, need(project, "gate_binary",
                              what="the binary that must link no emulator"))
    q = shlex.quote(g)
    script = (
        f"if [ ! -f {q} ]; then "
        f"echo 'GATE FAIL: {g} not built'; exit 1; fi; "
        f"if nm -u {q} | grep -Eiq 'm68k|musashi'; then "
        f"echo 'GATE FAIL: emulator symbols present in {g}'; exit 1; "
        f"else echo 'GATE PASS: {g} links no m68k/musashi symbols'; fi"
    )
    return [("no-emulator link gate", ["sh", "-c", script])]


# id, label, kind, description, builder
STAGES = [
    ("ingest", "Ingest WHDLoad slave", "auto",
     "Extract the slave code hunk and record its SHA-256.", _ingest),
    ("modules", "Extract overlays", "auto",
     "Parse the LOADER descriptor table and BOND-depack each overlay.", _modules),
    ("discover", "Discover code", "auto",
     "Capstone recursive reachable-code discovery into an IRA config.", _discover),
    ("assemble", "Reassemble", "auto",
     "Reassemble the overlays with vasmm68k.", _assemble),
    ("verify", "Verify byte-exact", "auto",
     "cmp the rebuilt overlays against the originals.", _verify),
    ("oracle", "Musashi oracle", "auto",
     "Run the emulator to a deterministic milestone (files/blits).", _oracle),
    ("unit-test", "Unit tests", "auto",
     "Fast unit checks plus the native blitter/video/input/audio selftests.", _unit_test),
    ("translate", "Translate to C", "assisted",
     "The manual/AI 68000->C step in src/recomp/runtime.c.", _translate),
    ("parity", "Parity diff", "auto",
     "Run native + recomp to the same frame; first divergence pinpoints the bug.", _parity),
    ("integration-test", "Integration tests", "auto",
     "Byte-exact verify plus the deterministic milestone integration checks.", _integration_test),
    ("recomp-test", "Recomp tests", "auto",
     "Run the recomp test suite and the no-emulator link audit.", _recomp_test),
    ("gate", "Release link gate", "auto",
     "Prove the target links no m68k/musashi symbols.", _gate),
]

STAGE_BY_ID = {s[0]: s for s in STAGES}

TRANSLATE_GUIDANCE = (
    "The 68000 -> C translation is the one assisted step: it cannot be fully\n"
    "automated.  Translated routines live in src/recomp/runtime.c and are\n"
    "checked routine-by-routine against the disassembly in asm/loader.asm.\n"
    "\n"
    "Workflow:\n"
    "  1. Run the 'parity' stage to find the first divergent record.\n"
    "  2. Locate that record's routine in asm/loader.asm (LAB_xxxx).\n"
    "  3. Retranslate it faithfully in src/recomp/runtime.c.\n"
    "  4. Re-run 'parity' and 'recomp-test' until the divergence is gone.\n"
)


def translate_report(project: dict) -> str:
    """Scan runtime.c and summarise the routines/types still fail-closed."""
    runtime = Path(project["root"]) / "src/recomp/runtime.c"
    try:
        text = runtime.read_text(encoding="utf-8")
    except OSError as exc:
        return f"(cannot read runtime.c: {exc})\n"
    out = ["Outstanding translation markers in src/recomp/runtime.c:"]
    for i, line in enumerate(text.splitlines(), 1):
        if "untranslated" in line or "BS_RECOMP_UNTRANSLATED" in line:
            out.append(f"  line {i}: {line.strip()[:110]}")
    if len(out) == 1:
        out.append("  (none found -- every reachable path is translated)")
    return "\n".join(out) + "\n"


# ---------------------------------------------------------------------------
# Runner
# ---------------------------------------------------------------------------


def run_steps(project: dict, steps, on_line) -> None:
    """Run a list of (label, argv[, out_path]) steps, streaming to on_line.

    An optional third tuple element redirects the step's stdout to that file
    (stderr is still streamed to on_line), used to capture the native/recomp
    state dumps for the parity diff without polluting the log."""
    for step in steps:
        label = step[0]
        argv = step[1]
        out_path = step[2] if len(step) > 2 else None
        on_line(f"\n== {label} ==\n$ {' '.join(argv)}"
                + (f" > {out_path}" if out_path else "") + "\n")
        if out_path:
            Path(out_path).parent.mkdir(parents=True, exist_ok=True)
            with open(out_path, "w") as out:
                proc = subprocess.Popen(
                    argv, cwd=project["root"],
                    stdout=out, stderr=subprocess.PIPE, text=True)
                assert proc.stderr is not None
                for line in proc.stderr:
                    on_line(line)
                code = proc.wait()
        else:
            proc = subprocess.Popen(
                argv, cwd=project["root"],
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            assert proc.stdout is not None
            for line in proc.stdout:
                on_line(line)
            code = proc.wait()
        if code != 0:
            raise RuntimeError(f"'{label}' failed with exit code {code}")


def mark_stage(project: dict, sid: str, status: str,
               error: str | None = None) -> None:
    """Record a stage's status (and optional error) in the project model."""
    record = {"status": status,
              "at": datetime.now().isoformat(timespec="seconds")}
    if error is not None:
        record["error"] = error
    project.setdefault("stages", {})[sid] = record


def run_stage(project: dict, stage_id: str, on_line) -> None:
    """Run one stage, updating the project status.

    An assisted stage records "manual", never "done" -- it is a note to the
    porter, not work the app performed.  A stage the project does not
    configure records "unsupported" and does not run anything.
    """
    stage = STAGE_BY_ID[stage_id]
    sid, label, kind, _desc, builder = stage
    on_line(f"\n### stage: {label} ({kind})\n")
    if kind == "assisted":
        on_line(TRANSLATE_GUIDANCE)
        if sid == "translate":
            on_line("\n" + translate_report(project))
        mark_stage(project, sid, "manual")
        return
    try:
        steps = builder(project)
    except StageUnsupported as exc:
        on_line(f"\nUNSUPPORTED: {exc}\n")
        mark_stage(project, sid, "unsupported", str(exc))
        return
    run_steps(project, steps, on_line)
    mark_stage(project, sid, "done")


def load_project(path: str | None):
    """Load a project from a JSON path, or fall back to the seeded title."""
    if path:
        data = json.loads(Path(path).read_text())
        return data, path
    return dict(BATTLE_SQUADRON), None


def save_project(project: dict, path: str | None) -> None:
    if path:
        Path(path).write_text(json.dumps(project, indent=2) + "\n")


def stage_status(project: dict, sid: str) -> str:
    return project.get("stages", {}).get(sid, {}).get("status", "pending")


def build_project_from_windows_dir(game_dir: str) -> dict:
    """Derive a project skeleton from a WHDLoad game directory.

    Every title-specific command is left EMPTY on purpose.  The old skeleton
    filled them in with Battle Squadron's oracle, make targets and gate binary,
    so a brand new title reported a mostly-green pipeline while rebuilding and
    verifying Battle Squadron.  Empty means the stage reports "unsupported"
    until the porter writes the real thing.
    """
    game = Path(game_dir)
    slaves = sorted(p for p in game.iterdir()
                    if p.suffix.lower() == ".slave")
    if not slaves:
        raise ValueError(f"no .slave file found under {game_dir}")
    slug = game.name.replace(" ", "_").lower()
    data = game / "data"
    has_loader = (data / "LOADER").exists()
    # Anything with a jump table or a plausible entry is a better discovery
    # target than a LOADER that may not exist; leave the real choice to the
    # porter but point at something that is actually present.
    candidates = sorted((p for p in data.iterdir() if p.is_file()),
                        key=lambda p: p.stat().st_size, reverse=True) \
        if data.is_dir() else []
    discovery_binary = str(data / "LOADER") if has_loader else (
        str(candidates[0]) if candidates else "")
    return {
        "name": game.name,
        "root": str(ROOT),
        "raw_slave": str(slaves[0]),
        "slave_out": f"original/{slug}/slave.bin",
        "data_dir": str(data),
        "modules_out": f"original/{slug}/modules",
        "manifest": f"docs/{slug}-module-map.json",
        "overlay_layout": "loader-bond" if has_loader else "none",
        "discovery": {
            "binary": discovery_binary,
            "base": "0x0",
            "entries": [],
            "output": f"build/{slug}.ira",
        },
        # Fill these in as the port acquires them.  Until then their stages
        # report "unsupported" rather than running another title's.
        "scripts": {"assemble": "", "verify": ""},
        "make_targets": {"unit_test": "", "integration_test": "",
                         "recomp_test": "", "recomp_dump": ""},
        "oracle": {"make_target": "", "binary": "", "frames": "5000",
                   "expect_files": "0", "expect_blits": "0"},
        "parity": {"native_frames": "6000", "game_frames": "600"},
        "build_prefix": f"build/{slug}_parity",
        "gate_binary": "",
        "stages": {},
    }


# ---------------------------------------------------------------------------
# Headless CLI
# ---------------------------------------------------------------------------


def run_headless(project: dict, project_path: str | None, stage_id: str) -> int:
    try:
        run_stage(project, stage_id, lambda line: sys.stdout.write(line))
        save_project(project, project_path)
        # Same contract as --run-all: 2 means the stage was skipped for want
        # of configuration, not that it passed.
        return 2 if stage_status(project, stage_id) == "unsupported" else 0
    except Exception as exc:  # noqa: BLE001 - report any stage failure
        print(f"\n[recomp-studio] {exc}", file=sys.stderr)
        mark_stage(project, stage_id, "failed", str(exc))
        save_project(project, project_path)
        return 1


def print_summary(project: dict, out=None) -> None:
    """Print every stage's real status.  A run that ends without this is how
    'four stages quietly rebuilt the wrong game' went unnoticed."""
    write = (out or sys.stdout).write
    write("\n\n########## PIPELINE SUMMARY: "
          f"{project['name']} ##########\n")
    tally: dict[str, int] = {}
    for sid, label, kind, _desc, _b in STAGES:
        record = project.get("stages", {}).get(sid, {})
        status = record.get("status", "pending")
        tally[status] = tally.get(status, 0) + 1
        note = record.get("error", "")
        if len(note) > 96:
            note = note[:93] + "..."
        write(f"  {status:<12} {sid:<18} {label}"
              + (f"\n{'':>15}{note}\n" if note else "\n"))
    write("  " + "  ".join(f"{k}={v}" for k, v in sorted(tally.items()))
          + "\n")
    if tally.get("unsupported"):
        write("  'unsupported' means the project JSON does not configure that "
              "stage for this\n  title.  Nothing ran -- do NOT read it as a "
              "pass.\n")


def run_all_headless(project: dict, project_path: str | None) -> int:
    """Run every stage in order, continuing past failures, and persist each
    stage's status.  Returns nonzero if any stage failed."""
    failed = 0
    for sid, label, _kind, _desc, _b in STAGES:
        print(f"\n\n########## STAGE: {label} ##########")
        try:
            run_stage(project, sid, lambda line: sys.stdout.write(line))
            print(f"[recomp-studio] {sid}: "
                  f"{stage_status(project, sid)}")
        except Exception as exc:  # noqa: BLE001
            print(f"\n[recomp-studio] {sid}: {exc}", file=sys.stderr)
            mark_stage(project, sid, "failed", str(exc))
            failed += 1
        save_project(project, project_path)
    print_summary(project)
    if failed:
        return 1
    # Exit 0 must mean "the pipeline ran".  A run where stages were skipped for
    # want of configuration is not a pass, so give it its own code rather than
    # letting a caller read silence as success.
    unsupported = sum(1 for sid, *_ in STAGES
                      if stage_status(project, sid) == "unsupported")
    return 2 if unsupported else 0


# ---------------------------------------------------------------------------
# GUI
# ---------------------------------------------------------------------------


class StudioApp:
    def __init__(self, root, project: dict, project_path: str | None):
        self.root = root
        self.project = project
        self.project_path = project_path
        self.log_queue: queue.Queue = queue.Queue()
        self.busy = False

        root.title(f"Amiga Recomp Studio — {project['name']}")
        root.geometry("980x640")

        left = tk.Frame(root)
        left.pack(side="left", fill="y", padx=8, pady=8)

        tk.Label(left, text="Pipeline", font=("TkDefaultFont", 11, "bold")).pack(anchor="w")
        self.tree = ttk.Treeview(
            left, columns=("status", "kind"), show="tree headings", height=len(STAGES))
        self.tree.heading("#0", text="Stage")
        self.tree.heading("status", text="Status")
        self.tree.heading("kind", text="Kind")
        self.tree.column("#0", width=200)
        self.tree.column("status", width=80, anchor="center")
        self.tree.column("kind", width=70, anchor="center")
        for sid, label, kind, _desc, _b in STAGES:
            self.tree.insert("", "end", iid=sid, text=label, values=("", kind))
        self.tree.pack(fill="both", expand=True)
        self.tree.bind("<<TreeviewSelect>>", self._on_select)

        self.desc = tk.Label(left, text="", wraplength=360, justify="left",
                             fg="#444444")
        self.desc.pack(anchor="w", pady=6, fill="x")

        self.btn_run = tk.Button(left, text="Run selected", command=self._run_selected)
        self.btn_run.pack(fill="x", pady=2)
        self.btn_all = tk.Button(left, text="Run all", command=self._run_all)
        self.btn_all.pack(fill="x", pady=2)
        self.btn_new = tk.Button(left, text="New project…", command=self._new_project)
        self.btn_new.pack(fill="x", pady=2)
        self.btn_open = tk.Button(left, text="Open project…", command=self._open_project)
        self.btn_open.pack(fill="x", pady=2)

        right = tk.Frame(root)
        right.pack(side="left", fill="both", expand=True, padx=8, pady=8)

        info = (
            f"root:       {project['root']}\n"
            f"data dir:   {resolve(project, project['data_dir'])}\n"
            f"slave:      {resolve(project, project['raw_slave'])}\n"
            f"manifest:   {resolve(project, project['manifest'])}\n"
            f"oracle:     {resolve(project, project['oracle']['binary'])}"
        )
        tk.Label(right, text=info, justify="left", anchor="w",
                 fg="#333333").pack(fill="x")

        tk.Label(right, text="Log", font=("TkDefaultFont", 11, "bold")).pack(anchor="w")
        self.log = scrolledtext.ScrolledText(right, state="disabled", wrap="word")
        self.log.pack(fill="both", expand=True)

        self._refresh()
        self.root.after(100, self._poll)

    # -- helpers ------------------------------------------------------------
    def _log(self, text: str) -> None:
        self.log.configure(state="normal")
        self.log.insert("end", text)
        self.log.see("end")
        self.log.configure(state="disabled")

    def _set_status(self, sid: str, status: str) -> None:
        self.tree.set(sid, "status", status)

    def _refresh(self) -> None:
        for sid, _label, _kind, _desc, _b in STAGES:
            self._set_status(sid, stage_status(self.project, sid))

    def _on_select(self, _event) -> None:
        selection = self.tree.selection()
        if not selection:
            return
        sid = selection[0]
        _sid, _label, _kind, desc, _b = STAGE_BY_ID[sid]
        self.desc.configure(text=desc)

    def _poll(self) -> None:
        try:
            while True:
                item = self.log_queue.get_nowait()
                if item[0] == "line":
                    self._log(item[1])
                elif item[0] == "status":
                    self._set_status(item[1], item[2])
                elif item[0] == "done":
                    self.busy = False
                    self.btn_run.configure(state="normal")
                    self.btn_all.configure(state="normal")
                    if item[1]:
                        self._log(f"\n[stage {item[2]}]\n")
        except queue.Empty:
            pass
        self.root.after(100, self._poll)

    def _worker(self, stage_id: str) -> None:
        try:
            run_stage(self.project, stage_id,
                      lambda line: self.log_queue.put(("line", line)))
            self.log_queue.put(("status", stage_id, "done"))
            self.log_queue.put(("done", True, stage_id))
        except Exception as exc:  # noqa: BLE001
            mark_stage(self.project, stage_id, "failed", str(exc))
            self.log_queue.put(("line", f"\n[recomp-studio] {exc}\n"))
            self.log_queue.put(("status", stage_id, "failed"))
            self.log_queue.put(("done", False, stage_id))
        save_project(self.project, self.project_path)

    def _launch(self, stage_id: str) -> None:
        if self.busy:
            return
        self.busy = True
        self.btn_run.configure(state="disabled")
        self.btn_all.configure(state="disabled")
        self._set_status(stage_id, "running")
        threading.Thread(target=self._worker, args=(stage_id,),
                         daemon=True).start()

    def _run_selected(self) -> None:
        selection = self.tree.selection()
        if selection:
            self._launch(selection[0])

    def _run_all(self) -> None:
        def worker():
            for sid, _label, _kind, _desc, _b in STAGES:
                self.log_queue.put(("status", sid, "running"))
                try:
                    run_stage(self.project, sid,
                              lambda line, s=sid: self.log_queue.put(("line", line)))
                    self.log_queue.put(("status", sid, "done"))
                except Exception as exc:  # noqa: BLE001
                    mark_stage(self.project, sid, "failed", str(exc))
                    self.log_queue.put(("line", f"\n[recomp-studio] {exc}\n"))
                    self.log_queue.put(("status", sid, "failed"))
                    self.log_queue.put(("line", "\nStopping: a stage failed.\n"))
                    break
            self.log_queue.put(("done", False, "all"))
            save_project(self.project, self.project_path)

        if self.busy:
            return
        self.busy = True
        self.btn_run.configure(state="disabled")
        self.btn_all.configure(state="disabled")
        threading.Thread(target=worker, daemon=True).start()

    def _new_project(self) -> None:
        game_dir = filedialog.askdirectory(
            title="Select the WHDLoad game directory (contains the .slave + data/)")
        if not game_dir:
            return
        try:
            project = build_project_from_windows_dir(game_dir)
        except ValueError as exc:
            messagebox.showerror("New project", str(exc))
            return
        out = filedialog.asksaveasfilename(
            title="Save project JSON", defaultextension=".json",
            initialfile=f"{project['name']}.json")
        if not out:
            return
        save_project(project, out)
        self.project = project
        self.project_path = out
        self.root.title(f"Amiga Recomp Studio — {project['name']}")
        self._refresh()

    def _open_project(self) -> None:
        path = filedialog.askopenfilename(
            title="Open project JSON", filetypes=[("JSON", "*.json")])
        if not path:
            return
        project, _ = load_project(path)
        self.project = project
        self.project_path = path
        self.root.title(f"Amiga Recomp Studio — {project['name']}")
        self._refresh()


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------


def main() -> int:
    parser = argparse.ArgumentParser(description="Amiga Recomp Studio")
    parser.add_argument("--project", help="project JSON file (default: seeded Battle Squadron)")
    parser.add_argument("--stage", help="run one stage headlessly and exit")
    parser.add_argument("--run-all", action="store_true",
                        help="run every stage in order and exit")
    parser.add_argument("--list", action="store_true", help="list stages and status")
    parser.add_argument("--summary", action="store_true",
                        help="print the recorded per-stage status and exit")
    parser.add_argument("--new-from", metavar="GAME_DIR",
                        help="write a project skeleton for a WHDLoad game "
                             "directory and exit")
    parser.add_argument("--out", metavar="JSON",
                        help="where --new-from writes the skeleton")
    args = parser.parse_args()

    if args.new_from:
        skeleton = build_project_from_windows_dir(args.new_from)
        out = args.out or f"{skeleton['name'].replace(' ', '_')}.json"
        save_project(skeleton, out)
        print(f"wrote {out}")
        print(f"overlay_layout={skeleton['overlay_layout']}; "
              f"discovery binary={skeleton['discovery']['binary'] or '(none)'}")
        print("Fill in discovery.base/entries, scripts.*, make_targets.*, "
              "oracle.* and gate_binary.\nUntil then those stages report "
              "'unsupported' -- that is correct, not a failure.")
        return 0

    project, project_path = load_project(args.project)

    if args.list:
        for sid, label, kind, _desc, _b in STAGES:
            print(f"{sid:18s} [{kind:8s}] "
                  f"{stage_status(project, sid):12s} {label}")
        return 0

    if args.summary:
        print_summary(project)
        return 0

    if args.stage:
        if args.stage not in STAGE_BY_ID:
            print(f"unknown stage '{args.stage}'. use --list", file=sys.stderr)
            return 2
        return run_headless(project, project_path, args.stage)

    if args.run_all:
        return run_all_headless(project, project_path)

    if tk is None:
        print("tkinter is not available; use --stage/--list for headless runs",
              file=sys.stderr)
        return 1
    root = tk.Tk()
    StudioApp(root, project, project_path)
    root.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
