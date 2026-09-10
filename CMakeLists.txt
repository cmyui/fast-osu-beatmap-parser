cmake_minimum_required(VERSION 3.26)
# Native release builds retain assertions; tests also explicitly enable them.
# Python extension targets define NDEBUG independently.
set(CMAKE_CXX_FLAGS_RELEASE "-O3" CACHE STRING "Release C++ flags")
set(CMAKE_C_FLAGS_RELEASE "-O2" CACHE STRING "Release C flags")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g" CACHE STRING "Debug C++ flags")

project(fosu VERSION 0.2.0 LANGUAGES C CXX)
include(CTest)
include(GNUInstallDirs)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(FOSU_ISA auto CACHE STRING "Native targets: auto, scalar, avx2, neon")
set_property(CACHE FOSU_ISA PROPERTY STRINGS auto scalar avx2 neon)
option(FOSU_SANITIZE "Enable address, undefined and float-cast sanitizers" OFF)
option(FOSU_ARENA_TELEMETRY "Instrument arena use in the profiling driver" OFF)
set(_python OFF)
if(SKBUILD)
  set(_python ON)
endif()
option(FOSU_BUILD_PYTHON "Build the CPython stable-ABI extension" ${_python})
set(_bundled OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT FOSU_SANITIZE AND NOT SKBUILD)
  set(_bundled ON)
endif()
if(DEFINED ENV{FOSU_BUNDLE_RUNTIME})
  set(_bundled "$ENV{FOSU_BUNDLE_RUNTIME}")
elseif(DEFINED ENV{CIBUILDWHEEL} AND "$ENV{CIBUILDWHEEL}" STREQUAL "1")
  set(_bundled ON)
endif()
option(FOSU_BUNDLE_RUNTIME "Bundle the Linux C++ runtime" ${_bundled})
set(_processor "${CMAKE_SYSTEM_PROCESSOR}")
if(APPLE AND CMAKE_OSX_ARCHITECTURES)
  set(_processor "${CMAKE_OSX_ARCHITECTURES}")
endif()
set(_x86 OFF)
if(_processor MATCHES "^(x86_64|AMD64|amd64)$")
  set(_x86 ON)
endif()
set(_arm64 OFF)
if(_processor MATCHES "^(aarch64|arm64|ARM64)$")
  set(_arm64 ON)
endif()
# Header-only tools select instructions at compile time; compiled products dispatch.
set(_header_isa "${FOSU_ISA}")
if(FOSU_ISA STREQUAL "auto")
  if(_x86)
    set(_header_isa avx2)
  elseif(_arm64)
    set(_header_isa neon)
  else()
    set(_header_isa scalar)
  endif()
endif()
if(NOT FOSU_ISA MATCHES "^(auto|scalar|avx2|neon)$")
  message(FATAL_ERROR "FOSU_ISA must be auto, scalar, avx2 or neon")
endif()
if(FOSU_ISA STREQUAL "avx2" AND NOT _x86)
  message(FATAL_ERROR "AVX2 requires an x86-64 target; use a cross-compilation toolchain")
endif()
if(FOSU_ISA STREQUAL "neon" AND NOT _arm64)
  message(FATAL_ERROR "NEON requires an AArch64 target")
endif()
if(NOT CMAKE_SYSTEM_NAME MATCHES "^(Linux|Darwin)$")
  message(FATAL_ERROR "fosu currently supports Linux and macOS")
endif()

add_library(fosu_headers INTERFACE)
add_library(fosu::headers ALIAS fosu_headers)
set_target_properties(fosu_headers PROPERTIES EXPORT_NAME headers)
target_include_directories(fosu_headers INTERFACE $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(fosu_headers INTERFACE cxx_std_20)
target_sources(fosu_headers INTERFACE FILE_SET HEADERS BASE_DIRS src FILES
  src/fosu/arena.h
  src/fosu/beatmap.h
  src/fosu/beatmap_header.h
  src/fosu/enums.h
  src/fosu/io.h
  src/fosu/legacy_rules.h
  src/fosu/mods.h
  src/fosu/slider_geometry.h
  src/fosu/slider_path.h
  src/fosu/slider_event.h
  src/fosu/slider_events.h
  src/fosu/stacking.h
  src/fosu/slider_timing.h
  src/fosu/os.h
  src/fosu/parse_options.h
  src/fosu/parser.h
  src/fosu/result.h
  src/fosu/runtime.h
  src/fosu/engine/hit_objects/common_fields.h
  src/fosu/engine/hit_objects/object_types.h
  src/fosu/engine/hit_objects/samples.h
  src/fosu/engine/hit_objects/slider_fields.h
  src/fosu/engine/hit_objects/sliders.h
  src/fosu/engine/parse_document.h
  src/fosu/engine/parsing/chunk_list.h
  src/fosu/engine/parsing/field_values.h
  src/fosu/engine/parsing/key_value.h
  src/fosu/engine/parsing/lines.h
  src/fosu/engine/parsing/numbers.h
  src/fosu/engine/parsing/sample_sets.h
  src/fosu/engine/parsing/section_names.h
  src/fosu/engine/parsing/string_lookup.h
  src/fosu/engine/parsing_engine.h
  src/fosu/engine/primitives/byte_scan.h
  src/fosu/engine/primitives/digit_groups.h
  src/fosu/engine/primitives/packed_digits.h
  src/fosu/engine/primitives/vector_ops.h
  src/fosu/engine/sections/colours.h
  src/fosu/engine/sections/difficulty.h
  src/fosu/engine/sections/editor.h
  src/fosu/engine/sections/events.h
  src/fosu/engine/sections/general.h
  src/fosu/engine/sections/hit_objects.h
  src/fosu/engine/sections/metadata.h
  src/fosu/engine/sections/timing_points.h
  src/fosu/engine/third_party/fast_float.h
  src/fosu/engine/timing_points/beat_length.h
  src/fosu/engine/timing_points/point.h
)

function(fosu_profile target)
  target_compile_definitions(${target} PRIVATE $<$<CONFIG:Debug>:_GLIBCXX_DEBUG>)
  if(FOSU_SANITIZE)
    target_compile_options(${target} PRIVATE -O1 -g -fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined,float-cast-overflow)
  endif()
endfunction()

# Product policy stays here; the header-only target imposes no tuning flags.
include(cmake/Hardening.cmake)
function(fosu_configure target isa product)
  target_link_libraries(${target} PRIVATE fosu_headers)
  target_compile_options(${target} PRIVATE -Wall -Wextra)
  if(isa STREQUAL "scalar")
    target_compile_definitions(${target} PRIVATE FOSU_DISABLE_SIMD)
  endif()
  if(_x86 AND isa STREQUAL "scalar")
    target_compile_options(${target} PRIVATE -march=x86-64)
  endif()
  if(product STREQUAL "python")
    target_compile_options(${target} PRIVATE -O3 -g0)
    target_compile_definitions(${target} PRIVATE NDEBUG)
    if(isa STREQUAL "avx2")
      target_compile_options(${target} PRIVATE -mavx2 -mbmi -mbmi2)
    endif()
  elseif(isa STREQUAL "avx2")
    target_compile_options(${target} PRIVATE -march=x86-64-v3)
  endif()
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if(isa STREQUAL "avx2")
      target_compile_options(${target} PRIVATE -mtune=znver4)
    endif()
    if(isa STREQUAL "avx2" OR product STREQUAL "python")
      target_compile_options(${target} PRIVATE -fno-plt)
    endif()
  endif()
  fosu_harden(${target})
  fosu_profile(${target})
endfunction()
function(fosu_runtime target)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if(FOSU_BUNDLE_RUNTIME)
      target_link_options(${target} PRIVATE -static-libstdc++ -static-libgcc)
    endif()
    target_link_options(${target} PRIVATE -Wl,--exclude-libs,ALL)
  endif()
endfunction()
function(fosu_executable target source)
  add_executable(${target} EXCLUDE_FROM_ALL ${source})
  fosu_configure(${target} "${_header_isa}" native)
endfunction()

# The core owns Parser lifetime and storage. ISA libraries execute the shared parser.
function(fosu_dispatch target product)
  fosu_configure(${target} scalar "${product}")
  target_sources(${target} PRIVATE src/fosu/engine/runtime/loader.cc)
  target_link_libraries(${target} PRIVATE ${CMAKE_DL_LIBS})
  target_compile_definitions(${target} PRIVATE
    FOSU_DEFAULT_BACKEND="${FOSU_ISA}"
    FOSU_ENGINE_SUFFIX="${CMAKE_SHARED_LIBRARY_SUFFIX}"
    FOSU_MANAGED_ARENA_CLEANUP)
  set_target_properties(${target} PROPERTIES
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
  set(variant "")
  if(_x86 AND NOT FOSU_ISA STREQUAL "scalar")
    set(variant avx2)
    target_compile_definitions(${target} PRIVATE FOSU_HAS_AVX2)
  elseif(_arm64 AND NOT FOSU_ISA STREQUAL "scalar")
    set(variant neon)
    target_compile_definitions(${target} PRIVATE FOSU_HAS_NEON)
  endif()
  if(variant)
    set(engine fosu_${product}_${variant})
    if(NOT TARGET ${engine})
      add_library(${engine} SHARED src/fosu/engine/runtime/entry.cc)
      fosu_configure(${engine} "${variant}" "${product}")
      fosu_runtime(${engine})
      set_target_properties(${engine} PROPERTIES
        OUTPUT_NAME fosu_engine_${variant}
        CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
      if(product STREQUAL "python")
        set_target_properties(${engine} PROPERTIES
          LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/python-engine")
        install(TARGETS ${engine} LIBRARY DESTINATION fosu)
      else()
        install(TARGETS ${engine} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
      endif()
    endif()
    add_dependencies(${target} ${engine})
  endif()
endfunction()

if(NOT SKBUILD)
  add_library(fosu SHARED src/fosu/runtime.cc)
  add_library(fosu::fosu ALIAS fosu)
  target_link_libraries(fosu PUBLIC fosu_headers)
  fosu_dispatch(fosu native)
  fosu_runtime(fosu)
  set_target_properties(fosu PROPERTIES CXX_VISIBILITY_PRESET hidden)
  target_include_directories(fosu PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
  install(TARGETS fosu fosu_headers EXPORT fosuTargets
    FILE_SET HEADERS DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  install(FILES src/fosu/engine/third_party/fast_float.md DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/fosu/engine/third_party)
  install(EXPORT fosuTargets NAMESPACE fosu:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fosu)
  include(CMakePackageConfigHelpers)
  configure_package_config_file(cmake/fosuConfig.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/fosuConfig.cmake INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fosu)
  write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/fosuConfigVersion.cmake COMPATIBILITY SameMajorVersion)
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/fosuConfig.cmake ${CMAKE_CURRENT_BINARY_DIR}/fosuConfigVersion.cmake DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fosu)
  include(cmake/Development.cmake)
endif()
if(FOSU_BUILD_PYTHON)
  include(cmake/Python.cmake)
endif()
