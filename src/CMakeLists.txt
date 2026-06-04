# Rebellion
#
# File: CMakeLists.txt
# Author: (C) Björn Kalkbrenner <terminar@cyberphoria.org> 2020-2023
# License: LGPLv3

add_subdirectory(librebellion)
add_subdirectory(host)

# The Go rebelliond daemon needs Go + TDM-GCC and is broken on Windows (see
# README "Build notes" and SPEC §0). Nothing in the Mixxx Studio Bridge / v0.0
# path uses it, so it's off by default. Flip ON if you specifically want it.
option(REBELLION_BUILD_REBELLIOND "Build the Go rebelliond daemon (needs Go+TDM-GCC; broken on Windows)" OFF)
if(REBELLION_BUILD_REBELLIOND)
    add_subdirectory(rebelliond)
endif()
