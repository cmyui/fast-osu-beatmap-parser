# Hardening for compiled products and their benchmarks. Header-only consumers
# own their build policy; the freestanding one-shot does not use this function.
set(_fosu_fortify 2)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  include(CheckCXXSourceCompiles)
  include(CMakePushCheckState)
  cmake_push_check_state(RESET)
  set(CMAKE_REQUIRED_FLAGS "-O2 -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3")
  check_cxx_source_compiles("#include <features.h>
    #if !defined(__USE_FORTIFY_LEVEL) || __USE_FORTIFY_LEVEL != 3
    #error Fortify 3 is unavailable
    #endif
    int main() { return 0; }" FOSU_HAS_FORTIFY_3)
  cmake_pop_check_state()
  if(FOSU_HAS_FORTIFY_3)
    set(_fosu_fortify 3)
  endif()
endif()

function(fosu_harden target)
  target_compile_options(${target} PRIVATE -fstack-protector-strong)
  # Sanitizers supply their own instrumentation; glibc fortification requires
  # optimization and can interfere with sanitizer diagnostics.
  if(NOT FOSU_SANITIZE)
    target_compile_options(${target} PRIVATE
      "$<$<NOT:$<CONFIG:Debug>>:-U_FORTIFY_SOURCE;-D_FORTIFY_SOURCE=${_fosu_fortify}>")
  endif()
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_options(${target} PRIVATE -fstack-clash-protection)
    target_link_options(${target} PRIVATE -Wl,-z,relro,-z,now,-z,noexecstack)
    get_target_property(type ${target} TYPE)
    if(type STREQUAL "EXECUTABLE")
      target_compile_options(${target} PRIVATE -fPIE)
      target_link_options(${target} PRIVATE -pie)
    endif()
  endif()
endfunction()
