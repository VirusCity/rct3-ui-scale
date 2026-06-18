# RCT3 UI Scaling Mod — d3d9.dll proxy + hook
#
# Builds a 32-bit `d3d9.dll` that proxies the real Direct3D 9 and hooks the
# device to rescale the game's UI. See CLAUDE.md for the full design.
#
# Configure/build (from a Developer prompt or via the preset):
#   cmake --preset vs2026-x86
#   cmake --build --preset vs2026-x86-release
#
# The HARD requirement is a 32-bit (x86 / Win32) build — RCT3 is a 32-bit app.

cmake_minimum_required(VERSION 3.21)

# Honour CMAKE_MSVC_RUNTIME_LIBRARY in this project *and* in vendored deps.
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
cmake_policy(SET CMP0091 NEW)

project(rct3_ui_scale LANGUAGES C CXX)

# --- Enforce 32-bit -----------------------------------------------------------
# A 64-bit DLL cannot be loaded by the 32-bit game. Fail loudly at configure
# time rather than producing an unusable artifact.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR
    "RCT3 is 32-bit; this mod MUST build for x86/Win32 (4-byte pointers).\n"
    "Detected ${CMAKE_SIZEOF_VOID_P}-byte pointers. With the Visual Studio "
    "generator, configure with:  -A Win32  (or use the provided CMake preset).")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static CRT so the deployed DLL has no vcruntime/msvcp dependency on the
# player's machine.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# --- Vendored MinHook (x86 static lib) ---------------------------------------
add_subdirectory(third_party/minhook)

# --- The proxy DLL ------------------------------------------------------------
add_library(d3d9 SHARED
  src/dllmain.cpp
  src/d3d9_hooks.cpp
  src/d3d9_proxy_exports.cpp
  src/ui_scale.cpp
  src/frame_inspect.cpp
  src/input_remap.cpp
  src/config.cpp
  src/logging.cpp
  src/d3d9.def        # module-definition file: exact export names + ordinals
)

target_include_directories(d3d9 PRIVATE src)
target_compile_features(d3d9 PRIVATE cxx_std_17)

# IMPORTANT: do NOT link the system d3d9.lib import library. Our output is
# itself named d3d9.dll; a static import of d3d9 would create a self-referential
# import. The real d3d9.dll is resolved at runtime via LoadLibrary/GetProcAddress.
target_link_libraries(d3d9 PRIVATE minhook)

# Output exactly "d3d9.dll" (no "lib" prefix).
set_target_properties(d3d9 PROPERTIES OUTPUT_NAME "d3d9" PREFIX "")

# Generate a .pdb in all configs to aid blind debugging of crash reports.
target_compile_options(d3d9 PRIVATE $<$<CONFIG:Release>:/Zi>)
target_link_options(d3d9 PRIVATE $<$<CONFIG:Release>:/DEBUG /OPT:REF /OPT:ICF>)

# Convenience: after each build, drop the DLL + the (pre-configured) .ini at the
# project root so they're trivial to copy into the game folder. Both root copies
# are gitignored.
add_custom_command(TARGET d3d9 POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          $<TARGET_FILE:d3d9> ${CMAKE_SOURCE_DIR}/d3d9.dll
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_SOURCE_DIR}/config/d3d9_uiscale.ini
          ${CMAKE_SOURCE_DIR}/d3d9_uiscale.ini
  COMMENT "Copying d3d9.dll + d3d9_uiscale.ini to project root for deployment")
