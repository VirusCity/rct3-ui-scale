# RCT3 UI Scale — proxy d3d9.dll that scales the RollerCoaster Tycoon 3 UI.
#
# 32-bit only (RCT3 is x86). Configure with the Win32 preset; the target
# emits d3d9.dll, deployed next to the game exe with config/d3d9_uiscale.ini.
cmake_minimum_required(VERSION 3.21)
project(rct3_ui_scale LANGUAGES CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "RCT3 is 32-bit — configure with the Win32 preset.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MinHook (BSD-2) is vendored under third_party/.
add_subdirectory(third_party/minhook)

add_library(d3d9 SHARED
  src/dllmain.cpp
  src/proxy.cpp
  src/proxy_exports.cpp
  src/selector.cpp
  src/core/log.cpp
  src/core/config.cpp
  src/core/scale.cpp
  src/core/patch.cpp
  src/core/state.cpp
  src/core/canvas_probe.cpp
  src/device/backbuffer_gate.cpp
  src/discovery/passive_discovery.cpp
  src/sig/sigscan.cpp
  src/hooks/timing_hook.cpp
  src/window/borderless.cpp
  src/apply/disable.cpp
  src/cache/strategy_cache.cpp
  src/d3d9.def
)

target_include_directories(d3d9 PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/minhook/include
)

target_link_libraries(d3d9 PRIVATE minhook d3d9.lib winmm)

# Output must be named exactly d3d9.dll with our .def-driven exports.
set_target_properties(d3d9 PROPERTIES
  OUTPUT_NAME "d3d9"
  PREFIX ""
)

if(MSVC)
  target_compile_options(d3d9 PRIVATE /W4 /permissive-)
  target_compile_definitions(d3d9 PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
endif()
