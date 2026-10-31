#!/usr/bin/env bash
# Builds Citron Lite with MinGW-w64 (C++20).
#   ./build_mingw.sh          build/CitronLite.exe and build/citron_lite_update.json
#   ./build_mingw.sh tests    build and run the tests
#   ./build_mingw.sh probe    print what the client reads from a running Minecraft (reads only)
#   ./build_mingw.sh run      build, then start it
#   ./build_mingw.sh clean    remove build/
set -e
cd "$(dirname "$0")"

MODE="${1:-}"
CXX=${CXX:-g++}
WINDRES=${WINDRES:-windres}
OBJ="${CITRON_OBJ:-build/obj}"
OUT="${CITRON_OUT:-build/CitronLite.exe}"
IMGUI="third_party/imgui"

if [ "$MODE" = "clean" ]; then
  rm -rf build
  echo "cleaned"
  exit 0
fi

TESTFLAGS="-std=c++20 -O2 -Iinclude -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -static -static-libgcc -static-libstdc++"
if [ "$MODE" = "tests" ]; then
  mkdir -p build
  run_test() {
    local name="$1"; shift
    echo "  TEST $name"
    "$CXX" $TESTFLAGS "$@" -o "build/test_$name.exe"
    "./build/test_$name.exe"
  }
  run_test signature_scan tools/signature_scan_tests.cpp
  run_test aim tools/aim_tests.cpp
  run_test config tools/config_io_tests.cpp src/config_io.cpp src/model.cpp -lcomdlg32 -lshell32
  run_test update_manifest tools/lite_manifest_tests.cpp
  run_test option_record tools/option_record_tests.cpp
  run_test game tools/game_tests.cpp src/core/game.cpp -lversion
  run_test names tools/names_tests.cpp
  run_test fastfall tools/fastfall_tests.cpp
  echo "  TEST ui"
  CITRON_LTO=0 CITRON_OBJ=build/obj-test CITRON_OUT=build/test/CitronLite.exe "$0" > build/test_ui_build.log 2>&1 || { cat build/test_ui_build.log; exit 1; }
  "$CXX" $TESTFLAGS tools/ui_tests.cpp src/config_io.cpp src/model.cpp -lcomdlg32 -lshell32 -o build/test_ui.exe
  ./build/test_ui.exe build/test/CitronLite.exe
  echo "all tests passed"
  exit 0
fi

if [ "$MODE" = "probe" ]; then
  mkdir -p build
  "$CXX" $TESTFLAGS tools/probe.cpp src/core/engine.cpp src/core/game.cpp src/core/bedrock.cpp src/core/game_bindings.cpp -lversion -luser32 -o build/probe.exe
  ./build/probe.exe "${@:2}"
  exit $?
fi

mkdir -p "$OBJ" "$(dirname "$OUT")"

LTO="-flto=auto"
[ "${CITRON_LTO:-1}" = "0" ] && LTO=""
INCLUDES="-I$IMGUI -I$IMGUI/backends -Iinclude -Isrc"
CXXFLAGS="-std=c++20 -O2 $LTO -ffunction-sections -fdata-sections -fstack-protector-strong -DNDEBUG -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -pthread $INCLUDES -Wall -Wno-unknown-pragmas"
LDFLAGS="-mwindows -static -static-libgcc -static-libstdc++ -pthread $LTO -Wl,--gc-sections -Wl,--dynamicbase -Wl,--high-entropy-va -Wl,--nxcompat"
LIBS="-ld3d11 -ldxgi -ld3dcompiler -ldwmapi -lgdi32 -luser32 -lole32 -limm32 -lcomdlg32 -lshell32 -lwinhttp -lbcrypt -lversion"

IMGUI_SRC="$IMGUI/imgui.cpp $IMGUI/imgui_draw.cpp $IMGUI/imgui_tables.cpp $IMGUI/imgui_widgets.cpp
$IMGUI/backends/imgui_impl_win32.cpp $IMGUI/backends/imgui_impl_dx11.cpp"
APP_SRC="src/main.cpp src/app.cpp src/model.cpp src/config_io.cpp src/platform_win32_dx11.cpp src/logo.cpp
src/telemetry.cpp src/updater.cpp src/ui_script.cpp
src/ui/draw.cpp src/ui/panel.cpp
src/core/engine.cpp src/core/game.cpp src/core/bedrock.cpp src/core/game_bindings.cpp
src/backend/backend.cpp src/backend/input.cpp src/backend/timing.cpp src/backend/world.cpp
src/modules/combat/aimassist.cpp src/modules/combat/triggerbot.cpp src/modules/combat/clicker.cpp
src/modules/combat/reach.cpp src/modules/combat/hitbox.cpp
src/modules/combat/throws.cpp
src/modules/movement/taps.cpp src/modules/movement/clock.cpp
src/modules/visuals/fov.cpp src/modules/visuals/fullbright.cpp src/modules/visuals/nametags.cpp
src/modules/utility/window_features.cpp"

set +e
OBJS=""
FAIL=0
for source in $IMGUI_SRC $APP_SRC; do
  object="$OBJ/$(echo "$source" | sed 's#[/.]#_#g').o"
  if [ -f "$object" ] && [ "$object" -nt "$source" ] && [ "${CITRON_REBUILD:-0}" = "0" ] && [ -z "$(find include src -newer "$object" -name '*.h' 2>/dev/null | head -1)" ]; then
    OBJS="$OBJS $object"
    continue
  fi
  echo "  CXX $source"
  if "$CXX" $CXXFLAGS -c "$source" -o "$object"; then
    OBJS="$OBJS $object"
  else
    echo "  *** FAILED: $source"
    FAIL=1
  fi
done
set -e
if [ "$FAIL" -ne 0 ]; then
  echo "compile errors, not linking"
  exit 1
fi

RES="$OBJ/citron_lite_res.o"
echo "  RC  citron.rc"
"$WINDRES" citron.rc -O coff --include-dir include -o "$RES"
OBJS="$OBJS $RES"

echo "  LINK $OUT"
"$CXX" $OBJS -o "$OUT" $LDFLAGS $LIBS
echo "built $OUT"

# The update manifest, from the version this was compiled with and the hash of what was just
# linked. The client refuses a download whose hash does not match.
MANIFEST="$(dirname "$OUT")/citron_lite_update.json"
VERSION_CODE=$(sed -n 's/.*kVersion = \([0-9]*\).*/\1/p' include/citron/version.h)
VERSION_NAME=$(sed -n 's/.*kVersionName = "\([^"]*\)".*/\1/p' include/citron/version.h)
SHA256=$(sha256sum "$OUT" | cut -d' ' -f1)
cat > "$MANIFEST" <<JSON
{
  "version": $VERSION_CODE,
  "versionName": "$VERSION_NAME",
  "sha256": "$SHA256",
  "url": "https://citron.lol/CitronLite.exe"
}
JSON
echo "manifest $MANIFEST (v$VERSION_NAME, code $VERSION_CODE)"

if [ "$MODE" = "run" ]; then
  ( cd "$(dirname "$OUT")" && "./$(basename "$OUT")" )
fi
