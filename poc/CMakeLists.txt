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
