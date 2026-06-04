# Mixxx Studio Bridge — v0.0 probe tools
#
# File: v0.0/CMakeLists.txt
# Builds the standalone C++ probes that link Rebellion's NIHIA core:
#   - nihia_monitor  (Test 0.0.2b, SPEC §1.5.1)
#   - display_probe  (Test 0.0.3,  SPEC §1.4)
#
# Wired into the build from the root CMakeLists via:
#   option(REBELLION_BUILD_V00_PROBES "Build v0.0 probe tools" ON)
#   if(REBELLION_BUILD_V00_PROBES) add_subdirectory("v0.0") endif()
#
# Both link the `librebellion` target (output name "rebellion") and reuse the
# same nlohmann-json single-header include that src/host uses.

# Resolve nlohmann-json include dir the same way src/host/CMakeLists.txt does.
get_target_property(nlohmann-json_SOURCE_DIR nlohmann-json SOURCE_DIR)

set(v00_INCLUDEDIRS
    ${PROJECT_SOURCE_DIR}/include
    ${nlohmann-json_SOURCE_DIR}
)

# --- nihia_monitor ------------------------------------------------------------
add_executable(nihia_monitor
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/nihia-monitor/main.cpp
)
target_include_directories(nihia_monitor PRIVATE ${v00_INCLUDEDIRS})
target_link_libraries(nihia_monitor librebellion)

# --- display_probe ------------------------------------------------------------
add_executable(display_probe
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/display-probe/main.cpp
)
target_include_directories(display_probe PRIVATE ${v00_INCLUDEDIRS})
target_link_libraries(display_probe librebellion)
