# Mixxx Studio Bridge — PoC tools
#
# Wired in from the root CMakeLists via REBELLION_BUILD_POC (default ON).
# Stage 1.5: waveform_decode — console decoder (sqlite3 + zlib + protobuf),
# no device. Stage 2 (render + knobs, links librebellion) is added later.

include(FetchContent)

# --- zlib (madler) — builds clean on MSVC --------------------------------------
# Vendored if present under poc/third_party/zlib, else fetched.
set(ZLIB_SRCDIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib")
if(EXISTS "${ZLIB_SRCDIR}/CMakeLists.txt")
    FetchContent_Declare(zlib_dep SOURCE_DIR "${ZLIB_SRCDIR}")
else()
    FetchContent_Declare(zlib_dep
        GIT_REPOSITORY https://github.com/madler/zlib.git
        GIT_TAG        v1.3.1
    )
endif()
set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(zlib_dep)

# madler/zlib exports the static target "zlibstatic" and puts its generated
# zconf.h in the build dir; collect both include dirs.
set(ZLIB_INCLUDE_DIRS ${zlib_dep_SOURCE_DIR} ${zlib_dep_BINARY_DIR})

# --- sqlite3 amalgamation ------------------------------------------------------
# Single-file build. Drop sqlite3.c + sqlite3.h into poc/third_party/sqlite3/,
# or let it fetch a known-good amalgamation mirror.
set(SQLITE_SRCDIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite3")
if(NOT EXISTS "${SQLITE_SRCDIR}/sqlite3.c")
    # Fetch the SQLite amalgamation (single sqlite3.c + sqlite3.h). If your box
    # blocks sqlite.org, vendor the two files into poc/third_party/sqlite3/
    # instead (download the amalgamation zip elsewhere, extract sqlite3.c/.h).
    FetchContent_Declare(sqlite3_amalgamation
        URL https://www.sqlite.org/2024/sqlite-amalgamation-3460100.zip
    )
    FetchContent_Populate(sqlite3_amalgamation)
    set(SQLITE_SRCDIR "${sqlite3_amalgamation_SOURCE_DIR}")
endif()

add_library(mxb_sqlite3 STATIC "${SQLITE_SRCDIR}/sqlite3.c")
target_include_directories(mxb_sqlite3 PUBLIC "${SQLITE_SRCDIR}")
target_compile_definitions(mxb_sqlite3 PRIVATE
    SQLITE_THREADSAFE=1
    SQLITE_OMIT_LOAD_EXTENSION=1)

# --- decode library + console tool ---------------------------------------------
add_library(mxb_decode STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/decode/waveform.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/decode/mixxxdb.cpp
)
target_include_directories(mxb_decode PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/decode
    ${ZLIB_INCLUDE_DIRS}
)
target_link_libraries(mxb_decode PUBLIC zlibstatic mxb_sqlite3)

add_executable(waveform_decode ${CMAKE_CURRENT_SOURCE_DIR}/decode/main.cpp)
target_link_libraries(waveform_decode PRIVATE mxb_decode)

# --- Stage 2: studio_waveform (links librebellion: drives screens + knobs) -----
get_target_property(nlohmann-json_SOURCE_DIR nlohmann-json SOURCE_DIR)
add_executable(studio_waveform
    ${CMAKE_CURRENT_SOURCE_DIR}/studio-waveform/main.cpp
)
target_include_directories(studio_waveform PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${nlohmann-json_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/studio-waveform
)
target_link_libraries(studio_waveform PRIVATE mxb_decode librebellion)

# --- Stage 3: Mixxx bridge input layer (loopMIDI -> decode -> deck state) -------
# Pure decode/resolve/model core (no device, no Lua) + a thin RtMidi listener.
# RtMidi is optional: when present the listener can open a loopMIDI port; without
# it, the library still builds and the decode core + tests run anywhere.
option(REBELLION_POC_RTMIDI "Build the Mixxx-bridge loopMIDI listener via RtMidi" ON)

add_library(mxb_mixxx_bridge STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/mixxx-bridge/mixxx_midi.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mixxx-bridge/deck_state.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mixxx-bridge/track_resolver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mixxx-bridge/mixxx_listener.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mixxx-bridge/midi_out.cpp
)
target_include_directories(mxb_mixxx_bridge PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/mixxx-bridge
)
target_link_libraries(mxb_mixxx_bridge PUBLIC mxb_decode)

if(REBELLION_POC_RTMIDI)
    # RtMidi is vendored as a single .cpp + header (poc/third_party/rtmidi) and
    # compiled straight in — no FetchContent (no configure-time network) and no
    # extra DLL beside the exe. Pick the platform backend + system MIDI lib.
    set(RTMIDI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/rtmidi")
    target_sources(mxb_mixxx_bridge PRIVATE ${RTMIDI_DIR}/RtMidi.cpp)
    target_include_directories(mxb_mixxx_bridge PUBLIC ${RTMIDI_DIR})
    target_compile_definitions(mxb_mixxx_bridge PUBLIC MXB_HAVE_RTMIDI=1)
    if(WIN32)
        target_compile_definitions(mxb_mixxx_bridge PRIVATE __WINDOWS_MM__=1)
        target_link_libraries(mxb_mixxx_bridge PUBLIC winmm)
    elseif(APPLE)
        target_compile_definitions(mxb_mixxx_bridge PRIVATE __MACOSX_CORE__=1)
        target_link_libraries(mxb_mixxx_bridge PUBLIC "-framework CoreMIDI"
                              "-framework CoreAudio" "-framework CoreFoundation")
    else()
        target_compile_definitions(mxb_mixxx_bridge PRIVATE __LINUX_ALSA__=1)
        find_package(Threads REQUIRED)
        target_link_libraries(mxb_mixxx_bridge PUBLIC asound Threads::Threads)
    endif()
endif()

# Off-device unit tests for the decode core (SysEx 7-bit unpack, MIDI tables,
# deck model). Runs anywhere; registered with CTest.
add_executable(mxb_bridge_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/mixxx-bridge/test_mixxx_midi.cpp
)
target_link_libraries(mxb_bridge_tests PRIVATE mxb_mixxx_bridge)
enable_testing()
add_test(NAME mxb_bridge_tests COMMAND mxb_bridge_tests)

# Outbound control-mapping unit tests (Studio control -> MIDI).
add_executable(mxb_control_map_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/mixxx-bridge/test_control_map.cpp
)
target_link_libraries(mxb_control_map_tests PRIVATE mxb_mixxx_bridge)
add_test(NAME mxb_control_map_tests COMMAND mxb_control_map_tests)

# Deck-panel render smoke test (no device).
add_executable(mxb_deck_panel_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/studio-bridge/test_deck_panel.cpp
)
target_include_directories(mxb_deck_panel_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/studio-waveform   # framebuffer.hpp + font
    ${CMAKE_CURRENT_SOURCE_DIR}/studio-bridge
)
target_link_libraries(mxb_deck_panel_tests PRIVATE mxb_mixxx_bridge)
add_test(NAME mxb_deck_panel_tests COMMAND mxb_deck_panel_tests)

# --- Stage 3 integration: studio_bridge (live Mixxx -> A/B deck screens) -------
add_executable(studio_bridge
    ${CMAKE_CURRENT_SOURCE_DIR}/studio-bridge/main.cpp
)
target_include_directories(studio_bridge PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${nlohmann-json_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/studio-waveform   # framebuffer.hpp + font
    ${CMAKE_CURRENT_SOURCE_DIR}/studio-bridge
)
target_link_libraries(studio_bridge PRIVATE mxb_mixxx_bridge librebellion)
