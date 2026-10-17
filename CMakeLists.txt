# Alternative build (CMake + MSVC toolchain). Produces custommodels.asi.
#
#   cmake -B build/cmake -A Win32 -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/cmake --config Release
#
# The MSBuild project in build/custommodels.vcxproj is the primary build.
cmake_minimum_required(VERSION 3.15)

# Honor CMAKE_MSVC_RUNTIME_LIBRARY (static CRT below).
cmake_policy(SET CMP0091 NEW)

project(custommodels CXX)

if(NOT WIN32)
  message(FATAL_ERROR "custommodels is a Win32 GTA:SA plugin; configure with the MSVC toolchain (-A Win32).")
endif()

file(GLOB_RECURSE CUSTOMMODELS_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_SOURCE_DIR}/src/*.cpp")

# the RakNet BitStream lives under vendor/, not src/, so add it explicitly.
list(APPEND CUSTOMMODELS_SOURCES "${CMAKE_SOURCE_DIR}/vendor/raknet/bitstream.cpp")

add_library(custommodels SHARED ${CUSTOMMODELS_SOURCES})

target_compile_features(custommodels PRIVATE cxx_std_17)

target_include_directories(custommodels PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/vendor"
    "${CMAKE_SOURCE_DIR}/vendor/urmem")

target_compile_definitions(custommodels PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)

target_link_libraries(custommodels PRIVATE wininet shell32)

# Static CRT: no runtime DLL dependency inside the game process.
set_property(TARGET custommodels PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Emit custommodels.asi directly (no "lib" prefix, .asi suffix).
set_target_properties(custommodels PROPERTIES PREFIX "" SUFFIX ".asi")
