cmake_minimum_required(VERSION 3.20)
project(CitronLite LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Threads REQUIRED)
if(MINGW)
    add_link_options(-static -static-libgcc -static-libstdc++)
endif()

set(IMGUI third_party/imgui)

add_executable(CitronLite WIN32
    src/main.cpp
    src/app.cpp
    src/model.cpp
    src/config_io.cpp
    src/platform_win32_dx11.cpp
    src/overlay.cpp
    src/logo.cpp
    src/telemetry.cpp
    src/updater.cpp
    src/ui_script.cpp
    src/ui/draw.cpp
    src/ui/panel.cpp
    src/ui/ingame.cpp
    src/core/engine.cpp
    src/core/game.cpp
    src/core/bedrock.cpp
    src/core/game_bindings.cpp
    src/backend/backend.cpp
    src/backend/input.cpp
    src/backend/timing.cpp
    src/backend/world.cpp
    src/modules/combat/aimassist.cpp
    src/modules/combat/triggerbot.cpp
    src/modules/combat/clicker.cpp
    src/modules/combat/reach.cpp
    src/modules/combat/hitbox.cpp
    src/modules/combat/throws.cpp
    src/modules/movement/taps.cpp
    src/modules/movement/clock.cpp
    src/modules/visuals/fov.cpp
    src/modules/visuals/fullbright.cpp
    src/modules/visuals/nametags.cpp
    src/modules/utility/window_features.cpp
    citron.rc
    ${IMGUI}/imgui.cpp
    ${IMGUI}/imgui_draw.cpp
    ${IMGUI}/imgui_tables.cpp
    ${IMGUI}/imgui_widgets.cpp
    ${IMGUI}/backends/imgui_impl_win32.cpp
    ${IMGUI}/backends/imgui_impl_dx11.cpp
)

target_include_directories(CitronLite PRIVATE include src ${IMGUI} ${IMGUI}/backends)
target_compile_definitions(CitronLite PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(CitronLite PRIVATE
    Threads::Threads d3d11 dxgi d3dcompiler dcomp dwmapi gdi32 user32 ole32 imm32 comdlg32 shell32 winhttp
    bcrypt version)

enable_testing()
function(citron_test name)
    add_executable(test_${name} ${ARGN})
    target_include_directories(test_${name} PRIVATE include)
    target_compile_definitions(test_${name} PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    add_test(NAME ${name} COMMAND test_${name})
endfunction()
citron_test(signature_scan tools/signature_scan_tests.cpp)
citron_test(aim tools/aim_tests.cpp)
citron_test(config tools/config_io_tests.cpp src/config_io.cpp src/model.cpp)
target_link_libraries(test_config PRIVATE comdlg32 shell32)
citron_test(update_manifest tools/lite_manifest_tests.cpp)
citron_test(option_record tools/option_record_tests.cpp)
citron_test(game tools/game_tests.cpp src/core/game.cpp)
target_link_libraries(test_game PRIVATE version)
citron_test(names tools/names_tests.cpp)
citron_test(slot_guard tools/slot_guard_tests.cpp src/core/engine.cpp)
citron_test(fastfall tools/fastfall_tests.cpp)
add_executable(test_ui tools/ui_tests.cpp src/config_io.cpp src/model.cpp)
target_include_directories(test_ui PRIVATE include)
target_compile_definitions(test_ui PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(test_ui PRIVATE comdlg32 shell32)
add_dependencies(test_ui CitronLite)
add_test(NAME ui COMMAND test_ui $<TARGET_FILE:CitronLite>)
