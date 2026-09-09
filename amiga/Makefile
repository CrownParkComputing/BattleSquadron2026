verify:
	./verify.sh

regen:
	./regen_asm.sh

MUSASHI_DIR ?= $(HOME)/BattleSquadron/src/musashi
MUSASHI = $(MUSASHI_DIR)/m68kcpu.c $(MUSASHI_DIR)/m68kops.c \
	$(MUSASHI_DIR)/m68kdasm.c $(MUSASHI_DIR)/softfloat/softfloat.c
NATIVE_CFLAGS = -DM68K_INSTRUCTION_HOOK=M68K_OPT_SPECIFY_HANDLER \
	-O2 -std=c11 -Wall -Wextra -include src/host/amiga.h \
	-I$(MUSASHI_DIR) -Isrc/host
RAYLIB_FLAGS = -I$(HOME)/.local/include $(HOME)/.local/lib/libraylib.a \
	-lm -lpthread -ldl -lGL -lX11

build/battle_squadron_native: src/host/main.c src/host/amiga.c \
		src/host/amiga.h src/host/whdload.c src/host/whdload.h $(MUSASHI)
	mkdir -p build
	$(CC) $(NATIVE_CFLAGS) -o $@ src/host/main.c src/host/amiga.c \
		src/host/whdload.c $(MUSASHI) -lm

build/hybris_native: src/host/hybris_native.c src/host/whdload.c \
		src/host/amiga.c src/host/amiga.h src/host/whdload.h $(MUSASHI)
	mkdir -p build
	$(CC) $(NATIVE_CFLAGS) -o $@ src/host/hybris_native.c \
		src/host/whdload.c src/host/amiga.c $(MUSASHI) -lm

build/hybris: src/host/hybris_run.c src/host/exeboot.c src/host/hunk.c \
		src/host/hybris_files.c src/host/hybris_loader.c \
		src/host/whdload.c src/host/amiga.c \
		src/host/amiga.h src/host/exeboot.h src/host/hunk.h \
		src/host/hybris_files.h src/host/hybris_loader.h $(MUSASHI)
	mkdir -p build
	$(CC) $(NATIVE_CFLAGS) -o $@ src/host/hybris_run.c src/host/exeboot.c \
		src/host/hunk.c src/host/hybris_files.c src/host/hybris_loader.c \
		src/host/whdload.c src/host/amiga.c $(MUSASHI) -lm

# The file server on its own: the disk map, track-to-file resolution and the
# tail clamp, with no 68000 and no disk image in the loop.
build/test_hybris_files: tests/test_hybris_files.c src/host/hybris_files.c \
		src/host/hybris_files.h
	mkdir -p build
	$(CC) -O2 -std=c11 -Wall -Wextra -Isrc/host -o $@ \
		tests/test_hybris_files.c src/host/hybris_files.c

hybris-test: build/test_hybris_files
	./build/test_hybris_files original/hybris

hybris: build/hybris

# The abandoned WHDLoad route, kept only because the slave analysis lives
# in it; the file route above is the one that boots.
hybris-whdload: build/hybris_native

native: build/battle_squadron_native

# First genuine recompilation component.  This target deliberately contains
# no Musashi sources and parity-checks the translated $AB46 routine against
# every real BOND stream in the owned install.
build/test_recomp_bond: tests/test_recomp_bond.c src/recomp/bond.c \
		src/recomp/bond.h src/recomp/overlay.c src/recomp/overlay.h
	mkdir -p build
	$(CC) -O2 -std=c11 -Wall -Wextra -Isrc/recomp -o $@ \
		tests/test_recomp_bond.c src/recomp/bond.c src/recomp/overlay.c

recomp-test: build/test_recomp_bond build/test_recomp_boot \
		build/test_ocs_video build/test_scroll_map build/test_bs_map \
		build/test_paula_audio build/test_bs_scroll_video
	./build/test_recomp_bond
	./build/test_recomp_boot
	./build/test_ocs_video
	./build/test_scroll_map
	./build/test_bs_map
	./build/test_paula_audio
	./build/test_bs_scroll_video
	@if nm -u build/test_recomp_bond | grep -Eiq 'm68k|musashi'; then \
		echo "recomp-test unexpectedly references a 68000 emulator"; exit 1; \
	else \
		echo "recomp-test: PASS (no Musashi/m68k runtime symbols)"; \
	fi
	@if nm -u build/test_recomp_boot | grep -Eiq 'm68k|musashi'; then \
		echo "recomp boot unexpectedly references a 68000 emulator"; exit 1; \
	else \
		echo "recomp boot: PASS (no Musashi/m68k runtime symbols)"; \
	fi
	@if nm -u build/test_ocs_video | grep -Eiq 'm68k|musashi'; then \
		echo "OCS video test unexpectedly references a 68000 emulator"; \
		exit 1; \
	else \
		echo "OCS video test: PASS (no Musashi/m68k runtime symbols)"; \
	fi
	@if nm -u build/test_scroll_map build/test_bs_map build/test_paula_audio \
		build/test_bs_scroll_video | \
		grep -Eiq 'm68k|musashi'; then \
		echo "map tests unexpectedly reference a 68000 emulator"; exit 1; \
	else \
		echo "map tests: PASS (no Musashi/m68k runtime symbols)"; \
	fi

build/test_paula_audio: tests/test_paula_audio.c \
		src/platform/paula_audio.c src/platform/paula_audio.h
	mkdir -p build
	$(CC) -O2 -std=c11 -Wall -Wextra -Isrc/platform -o $@ \
		tests/test_paula_audio.c src/platform/paula_audio.c -lm

build/test_bs_scroll_video: tests/test_bs_scroll_video.c \
		src/recomp/runtime.c src/recomp/runtime.h src/recomp/overlay.c \
		src/recomp/overlay.h src/recomp/bond.c src/recomp/bond.h \
		src/platform/ocs_video.c src/platform/ocs_video.h
	mkdir -p build
	$(CC) -O2 -std=c11 -Wall -Wextra -Isrc/recomp -Isrc/platform \
		-o $@ tests/test_bs_scroll_video.c src/recomp/runtime.c \
		src/recomp/overlay.c src/recomp/bond.c src/platform/ocs_video.c

build/test_recomp_boot: tests/test_recomp_boot.c src/recomp/runtime.c \
		src/recomp/runtime.h src/recomp/overlay.c src/recomp/overlay.h \
		src/recomp/bond.c src/recomp/bond.h
	mkdir -p build
	$(CC) -O2 -std=c11 -Wall -Wextra -Isrc/recomp -o $@ \
		tests/test_recomp_boot.c src/recomp/runtime.c src/recomp/overlay.c \
		src/recomp/bond.c

build/test_ocs_video: tests/test_ocs_video.c src/platform/ocs_video.c \
		src/platform/ocs_video.h src/platform/ocs_palette.c \
		src/platform/ocs_palette.h
	mkdir -p build
	$(CC) -O2 -std=c11 -Wall -Wextra -Isrc/platform -o $@ \
		tests/test_ocs_video.c src/platform/ocs_video.c \
		src/platform/ocs_palette.c

build/test_scroll_map: tests/test_scroll_map.c src/platform/scroll_map.c \
		src/platform/scroll_map.h
	mkdir -p build
	$(CC) -O2 -std=c11 -Wall -Wextra -Isrc/platform -o $@ \
		tests/test_scroll_map.c src/platform/scroll_map.c

build/test_bs_map: tests/test_bs_map.c src/recomp/bs_map.c \
		src/recomp/bs_map.h src/recomp/runtime.c src/recomp/runtime.h \
		src/recomp/overlay.c src/recomp/overlay.h src/recomp/bond.c \
		src/recomp/bond.h src/platform/scroll_map.c \
		src/platform/scroll_map.h src/platform/ocs_palette.c \
		src/platform/ocs_palette.h
	mkdir -p build
	$(CC) -O2 -std=c11 -Wall -Wextra -Isrc/recomp -Isrc/platform \
		-o $@ tests/test_bs_map.c src/recomp/bs_map.c \
		src/recomp/runtime.c src/recomp/overlay.c src/recomp/bond.c \
		src/platform/scroll_map.c src/platform/ocs_palette.c

map-test: build/test_scroll_map build/test_bs_map
	./build/test_scroll_map
	./build/test_bs_map
	@if nm -u build/test_bs_map | grep -Eiq 'm68k|musashi'; then \
		echo "map test unexpectedly references a 68000 emulator"; exit 1; \
	else \
		echo "map-test: PASS (no Musashi/m68k runtime symbols)"; \
	fi

build/battle_squadron_map_lab: src/recomp/map_preview.c \
		src/recomp/bs_map.c src/recomp/bs_map.h src/recomp/runtime.c \
		src/recomp/runtime.h src/recomp/overlay.c src/recomp/overlay.h \
		src/recomp/bond.c src/recomp/bond.h src/platform/scroll_map.c \
		src/platform/scroll_map.h src/platform/ocs_palette.c \
		src/platform/ocs_palette.h
	mkdir -p build
	$(CC) -O2 -std=c11 -Wall -Wextra -Isrc/recomp -Isrc/platform \
		-I$(HOME)/.local/include -o $@ src/recomp/map_preview.c \
		src/recomp/bs_map.c src/recomp/runtime.c src/recomp/overlay.c \
		src/recomp/bond.c src/platform/scroll_map.c \
		src/platform/ocs_palette.c $(RAYLIB_FLAGS)
	@if nm -u $@ | grep -Eiq 'm68k|musashi'; then \
		echo "map lab unexpectedly references a 68000 emulator"; exit 1; \
	else \
		echo "map lab: PASS (no Musashi/m68k runtime symbols)"; \
	fi

build/battle_squadron_map.rmap: build/battle_squadron_map_lab
	./build/battle_squadron_map_lab --extract $@ \
		--ppm build/battle_squadron_map.ppm --frames 2048 --headless

map-extract: build/battle_squadron_map.rmap

map-preview: build/battle_squadron_map_lab

show-map: build/battle_squadron_map_lab build/battle_squadron_map.rmap
	./build/battle_squadron_map_lab --trace build/battle_squadron_map.rmap

build/battle_squadron_recomp_preview: src/recomp/preview.c \
		src/recomp/runtime.c src/recomp/runtime.h src/recomp/overlay.c \
		src/recomp/overlay.h src/recomp/bond.c src/recomp/bond.h \
		src/platform/ocs_video.c src/platform/ocs_video.h \
		src/platform/paula_audio.c src/platform/paula_audio.h
	mkdir -p build
	$(CC) -O2 -std=c11 -Wall -Wextra -Isrc/recomp -Isrc/platform \
		-I$(HOME)/.local/include -o $@ src/recomp/preview.c \
		src/recomp/runtime.c src/recomp/overlay.c src/recomp/bond.c \
		src/platform/ocs_video.c src/platform/paula_audio.c \
		$(RAYLIB_FLAGS)
	@if nm -u $@ | grep -Eiq 'm68k|musashi'; then \
		echo "recomp preview unexpectedly references a 68000 emulator"; \
		exit 1; \
	else \
		echo "recomp preview: PASS (no Musashi/m68k runtime symbols)"; \
	fi

build/render_frame: tools/render_frame.c src/recomp/runtime.c \
		src/recomp/overlay.c src/recomp/bond.c src/platform/ocs_video.c
	mkdir -p build
	$(CC) -O2 -std=c11 -Wall -Wextra -Isrc/recomp -Isrc/platform -o $@ $^ -lm

build/render_at: tools/render_at.c src/recomp/runtime.c \
		src/recomp/overlay.c src/recomp/bond.c src/platform/ocs_video.c
	mkdir -p build
	$(CC) -O2 -std=c11 -Wall -Wextra -Isrc/recomp -Isrc/platform -o $@ $^ -lm

render-at: build/render_at

render-frame: build/render_frame

build/recomp_dump: tools/recomp_dump.c src/recomp/runtime.c \
		src/recomp/runtime.h src/recomp/overlay.c src/recomp/overlay.h \
		src/recomp/bond.c src/recomp/bond.h
	mkdir -p build
	$(CC) -O2 -std=c11 -Wall -Wextra -Isrc/recomp -o $@ \
		tools/recomp_dump.c src/recomp/runtime.c src/recomp/overlay.c \
		src/recomp/bond.c

recomp-dump: build/recomp_dump

recomp-preview: build/battle_squadron_recomp_preview

show-recomp: build/battle_squadron_recomp_preview
	./build/battle_squadron_recomp_preview

build/battle_squadron: src/host/frontend.c src/host/amiga.c \
		src/host/amiga.h src/host/whdload.c src/host/whdload.h \
		src/host/pad.c src/host/pad.h $(MUSASHI)
	mkdir -p build
	$(CC) $(NATIVE_CFLAGS) -I$(HOME)/.local/include -o $@ \
		src/host/frontend.c src/host/pad.c src/host/amiga.c \
		src/host/whdload.c $(MUSASHI) $(RAYLIB_FLAGS)

# Hybris, playable: the same chipset and the same pad handling, booted from
# files through the loader hook.
build/hybris_play: src/host/hybris_play.c src/host/pad.c src/host/pad.h \
		src/host/exeboot.c src/host/hunk.c src/host/hybris_files.c \
		src/host/hybris_loader.c src/host/whdload.c src/host/amiga.c \
		src/host/amiga.h src/host/exeboot.h src/host/hybris_loader.h \
		$(MUSASHI)
	mkdir -p build
	$(CC) $(NATIVE_CFLAGS) -I$(HOME)/.local/include -o $@ \
		src/host/hybris_play.c src/host/pad.c src/host/exeboot.c \
		src/host/hunk.c src/host/hybris_files.c src/host/hybris_loader.c \
		src/host/whdload.c src/host/amiga.c $(MUSASHI) $(RAYLIB_FLAGS)

play-hybris: build/hybris_play
	./build/hybris_play

playable: build/battle_squadron

run: build/battle_squadron
	./build/battle_squadron

unit-test: build/battle_squadron_native
	python3 -m unittest discover -s tools -p 'test_*.py' -v
	./build/battle_squadron_native --selftest

integration-test: build/battle_squadron_native
	./verify.sh
	python3 -m unittest discover -s tests -p 'test_integration_*.py' -v

test: recomp-test unit-test integration-test

native-smoke: build/battle_squadron_native
	./build/battle_squadron_native --selftest
	./build/battle_squadron_native --frames 50000 --expect-files 20 \
		--expect-blits 1000000	.PHONY: verify regen test unit-test integration-test native native-smoke \
	recomp-test recomp-preview show-recomp render-frame recomp-dump map-test map-extract \
	map-preview show-map playable run
