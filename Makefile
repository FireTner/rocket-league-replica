# Root Makefile — RocketLeague clone (native C++20, physics-first).
# Determinism: -ffp-contract=off -fno-fast-math everywhere in the sim path.
# Toolchain: g++ only (no cmake); SDL2 via sdl2-config; 720p renderer GL2.1.

CXX ?= g++
STD = -std=c++20
WARN = -Wall -Wextra
# Physics determinism flags (Sim.cpp documents these; do not remove).
FPFLAGS = -ffp-contract=off -fno-fast-math -fno-strict-aliasing
OPT = -O2
RS_INC = -Ithird_party/RocketSim/src
SRC_INC = -Isrc
SDL_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null)
SDL_LIBS := $(shell sdl2-config --libs 2>/dev/null)

RS_SRCS := $(shell find third_party/RocketSim/src -name '*.cpp')
BULLET_SRCS := $(shell find third_party/RocketSim/libsrc -name '*.cpp')
RS_OBJS := $(patsubst %.cpp,build/obj/%.o,$(RS_SRCS))
BULLET_OBJS := $(patsubst %.cpp,build/obj/%.o,$(BULLET_SRCS))

SIM_SRCS = src/sim/Sim.cpp
HEADLESS_SRCS = tools/headless_sim/main.cpp

.PHONY: all headless game render_stub tests tapes verify meshgen clean
all: build/headless_sim build/render_stub build/game tests /tmp/arena_test

build/librocketsim.a: $(RS_OBJS) $(BULLET_OBJS)
	mkdir -p $(dir $@)
	ar rcs $@ $^

build/obj/%.o: %.cpp
	mkdir -p $(dir $@)
	$(CXX) $(STD) $(WARN) $(OPT) $(FPFLAGS) $(RS_INC) -c $< -o $@

# Sim objects also need src/ includes (physics presets) + determinism flags.
build/obj/src/sim/Sim.o: src/sim/Sim.cpp
	mkdir -p $(dir $@)
	$(CXX) $(STD) $(WARN) $(OPT) $(FPFLAGS) $(RS_INC) $(SRC_INC) -c $< -o $@

# A2 analytic arena (dependency-free; links into sim users for fallback
# containment). Same determinism flags: it runs inside Step120.
build/obj/src/arena/ArenaGeometry.o: src/arena/ArenaGeometry.cpp
	mkdir -p $(dir $@)
	$(CXX) $(STD) $(WARN) $(OPT) $(FPFLAGS) $(SRC_INC) -c $< -o $@

ARENA_OBJ = build/obj/src/arena/ArenaGeometry.o

build/headless_sim: $(HEADLESS_SRCS) build/obj/src/sim/Sim.o $(ARENA_OBJ) build/librocketsim.a
	mkdir -p build
	$(CXX) $(STD) $(WARN) $(OPT) $(FPFLAGS) $(SRC_INC) $(RS_INC) $^ -o $@

build/render_stub: src/render/Renderer.cpp src/camera/Camera.cpp src/fx/FX.cpp src/models/Models.cpp src/models/Fennec.cpp src/render/stub_main.cpp
	mkdir -p build
	$(CXX) $(STD) $(WARN) $(OPT) $(SRC_INC) $(SDL_CFLAGS) $^ $(SDL_LIBS) -lGL -o $@

GAME_SRCS = src/game/main.cpp src/render/Renderer.cpp src/camera/Camera.cpp src/fx/FX.cpp src/models/Models.cpp src/models/Fennec.cpp src/input/Keyboard.cpp src/input/Gamepad.cpp src/menu/Menu.cpp src/settings/Settings.cpp src/audio/Audio.cpp

build/game: $(GAME_SRCS) build/obj/src/sim/Sim.o $(ARENA_OBJ) build/librocketsim.a
	mkdir -p build
	$(CXX) $(STD) $(WARN) $(OPT) $(FPFLAGS) $(SRC_INC) $(RS_INC) $(SDL_CFLAGS) $^ $(SDL_LIBS) -lGL -o $@

# A2 arena standalone test (CR + walls + pads + mesh budget).
/tmp/arena_test: src/arena/arena_test.cpp src/arena/ArenaGeometry.cpp
	$(CXX) $(STD) $(WARN) $(SRC_INC) $^ -o $@ && $@

game: build/game

headless: build/headless_sim
render_stub: build/render_stub

tests: /tmp/t_rt /tmp/t_kb /tmp/t_gp
/tmp/t_rt: tests/test_settings_roundtrip.cpp src/settings/Settings.cpp
	$(CXX) $(STD) $(WARN) $(SRC_INC) $^ -o $@ && $@
/tmp/t_kb: tests/test_keyboard_menu.cpp src/input/Keyboard.cpp src/input/Gamepad.cpp src/menu/Menu.cpp src/settings/Settings.cpp
	$(CXX) $(STD) $(WARN) $(SRC_INC) $(SDL_CFLAGS) $^ $(SDL_LIBS) -o $@ && $@
# A5 gamepad contract test (deadzone math, sign table, hot-plug, names).
/tmp/t_gp: tests/test_gamepad.cpp src/input/Gamepad.cpp src/settings/Settings.cpp
	$(CXX) $(STD) $(WARN) $(SRC_INC) $(SDL_CFLAGS) $^ $(SDL_LIBS) -o $@ && $@

# A6 frozen tapes through the real sim (MODE=MEASURED).
tapes: build/headless_sim
	./build/headless_sim --tape tools/tas/tapes/straight_boost_600.csv --ticks 600 --dump /tmp/straight.csv --seed 1
	./build/headless_sim --tape tools/tas/tapes/jump_hold_240.csv --ticks 240 --dump /tmp/jump.csv --seed 1
	./build/headless_sim --tape tools/tas/tapes/powerslide_circle_720.csv --ticks 720 --dump /tmp/slide.csv --seed 1

verify: build/headless_sim
	python3 tools/tas/replay.py validate tools/tas/tapes/straight_boost_600.csv
	python3 tools/verify/check.py || true

# Clean-room arena collision mesh (our own geometry, RocketSim .cmf format).
# Deterministic: re-running reproduces a byte-identical file. The output lives
# under collision_meshes/ which .gitignore excludes (never commit mesh data).
build/make_soccar_mesh: tools/meshgen/make_soccar_mesh.cpp
	mkdir -p build
	$(CXX) $(STD) $(WARN) $(OPT) $< -o $@

meshgen: build/make_soccar_mesh
	mkdir -p collision_meshes/soccar
	./build/make_soccar_mesh collision_meshes/soccar/soccar_cleanroom.cmf

clean:
	rm -rf build /tmp/t_rt /tmp/t_kb /tmp/t_gp
