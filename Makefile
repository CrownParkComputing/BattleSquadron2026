# Battle Squadron native port -- engine core + data decoder + headless parity runner.
CC      ?= gcc
CFLAGS  ?= -O2 -g -Wall -Wextra -Wno-unused-parameter
CFLAGS  += -Isrc -Isrc/engine

DATA    ?= /home/jon/BattleSquadron-Amiga/original/whdload/BattleSquadron/data
REFMODS ?= /home/jon/BattleSquadron-Amiga/original/modules

CORE    := src/bsdata.c src/overlay.c src/bond.c
ENGINE  := src/engine/engine.c $(wildcard src/behaviours/*.c)
HDRS    := src/engine/engine.h src/bsdata.h src/overlay.h src/bond.h

all: build/simrun build/bsdata_test build/framecmp

build:
	mkdir -p build build/assets_check

build/simrun: tools/simrun.c $(ENGINE) $(CORE) $(HDRS) | build
	$(CC) $(CFLAGS) tools/simrun.c $(ENGINE) $(CORE) -o $@

build/bsdata_test: tools/bsdata_test.c $(CORE) $(HDRS) | build
	$(CC) $(CFLAGS) tools/bsdata_test.c $(CORE) -o $@

# make test: bsdata identity (native renders == python renders) + a parity smoke
test: build/simrun build/bsdata_test
	sh tools/test_bsdata.sh
	sh tools/test_parity_smoke.sh

clean:
	rm -rf build

.PHONY: all test clean

# Fully native Android build.  The original modules are supplied locally at
# build time and are never copied into the repository.
ANDROID_DATA ?=
ANDROID_RAYLIB ?= ../raylib-src

android-debug:
	test -n "$(ANDROID_DATA)"
	./android/gradlew -p android assembleDebug \
		-PbattleSquadronDataDir="$(ANDROID_DATA)" -PraylibDir="$(ANDROID_RAYLIB)"

android-install: android-debug
	adb install -r android/app/build/outputs/apk/debug/app-debug.apk

.PHONY: android-debug android-install

build/framecmp: tools/framecmp.c src/render.c src/render.h $(ENGINE) $(CORE) $(HDRS) | build
	$(CC) $(CFLAGS) tools/framecmp.c src/render.c $(ENGINE) $(CORE) -o $@

build/titlecmp: tools/titlecmp.c src/render.c src/render.h $(ENGINE) $(CORE) $(HDRS) | build
	$(CC) $(CFLAGS) tools/titlecmp.c src/render.c $(ENGINE) $(CORE) -o $@

# sprite-catalog sheet checksum (tools/spritecheck.c); update deliberately
SPRITE_SHEET_SUM ?= 3D81E3F9

RAYLIB = -I$(HOME)/.local/include $(HOME)/.local/lib/libraylib.a -lm -lpthread -ldl -lGL -lX11

build/bsview: src/viewer.c src/render.c src/audio.c $(ENGINE) $(CORE) $(HDRS) src/render.h src/audio.h | build
	$(CC) $(CFLAGS) src/viewer.c src/render.c src/audio.c $(ENGINE) $(CORE) $(RAYLIB) -o $@

run: build/bsview
	./build/bsview
.PHONY: run

# pixel-verification suite: replay the parity captures and diff rendered
# frames against the oracle screenshots (see PROJECT.md session 3 for the
# expected numbers)
verify: build/framecmp build/titlecmp build/spritecheck
	./build/titlecmp
	./build/framecmp re/trace/shots/i_06000.ppm 6000 --autopilot --invuln --fbase 1481
	./build/framecmp re/trace/shots/i_12000.ppm 12000 --autopilot --invuln --fbase 1481
	./build/framecmp re/trace/shots/i_18000.ppm 18000 --autopilot --invuln --fbase 1481
	./build/framecmp re/trace/shots/f_10000.ppm 10000 --fire --autopilot --invuln --fbase 1389
	./build/framecmp re/trace/shots/f_20000.ppm 20000 --fire --autopilot --invuln --fbase 1389
	./build/framecmp re/trace/shots/d12500.ppm 12500 --demo --fbase 9125 --layers 23
	./build/framecmp re/trace/shots/d13200.ppm 13200 --demo --fbase 9125 --layers 23
	./build/framecmp re/trace/shots/d13500.ppm 13500 --demo --fbase 9125 --layers 23
	./build/framecmp re/trace/shots/d14500.ppm 14500 --demo --fbase 9125 --layers 23
	./build/framecmp re/trace/shots/d15500.ppm 15500 --demo --fbase 9125 --layers 23
	./build/framecmp re/trace/shots/d16500.ppm 16500 --demo --fbase 9125 --layers 23
	# sprite browser: every bob the engine draws is decoded pixel-identically to
	# render.c, and the whole catalog sheet hashes to a fixed value
	./build/spritecheck --sheet build/sprite_sheet.ppm --expect $(SPRITE_SHEET_SUM)

# boots straight into play with autofire, screenshots to build/smoke.png, exits
smoke: build/bsview
	./build/bsview --smoke 400
.PHONY: verify smoke

build/bobscan: tools/bobscan.c $(ENGINE) $(CORE) $(HDRS) | build
	$(CC) $(CFLAGS) tools/bobscan.c $(ENGINE) $(CORE) -o $@

build/spritecheck: tools/spritecheck.c src/render.c $(ENGINE) $(CORE) $(HDRS) src/render.h | build
	$(CC) $(CFLAGS) tools/spritecheck.c src/render.c $(ENGINE) $(CORE) -o $@
