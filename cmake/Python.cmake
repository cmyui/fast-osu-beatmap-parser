find_package(Python REQUIRED COMPONENTS Interpreter)

# FindPython does not always discover python3.lib in the Windows Python layouts
# used by wheel builders. The Stable ABI must link to that unversioned import
# library rather than python3XY.lib.
if(WIN32 AND NOT Python_SABI_LIBRARY)
  execute_process(
    COMMAND "${Python_EXECUTABLE}" -c
      "import subprocess, sys; subprocess.run([sys._base_executable, '-c', \"import sys; print(sys.base_prefix, end='')\"], check=True)"
    OUTPUT_VARIABLE _python_root
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
  )
  if(NOT EXISTS "${_python_root}/libs/python3.lib")
    message(FATAL_ERROR "python3.lib was not found beside the Windows Python installation")
  endif()
  set(Python_INCLUDE_DIR "${_python_root}/include" CACHE PATH "Python include directory" FORCE)
  set(Python_SABI_LIBRARY "${_python_root}/libs/python3.lib" CACHE FILEPATH
    "Python Stable ABI import library" FORCE)
endif()

find_package(Python REQUIRED COMPONENTS Development.SABIModule)
Python_add_library(_core MODULE USE_SABI 3.10 WITH_SOABI src/fosu/bindings/python.cc)
fosu_dispatch(_core python)
fosu_runtime(_core)
set_target_properties(_core PROPERTIES CXX_VISIBILITY_PRESET hidden
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/python-engine")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_options(_core PRIVATE "-Wl,--version-script=${PROJECT_SOURCE_DIR}/python/core.map")
  set_property(TARGET _core APPEND PROPERTY LINK_DEPENDS "${PROJECT_SOURCE_DIR}/python/core.map")
elseif(APPLE)
  target_link_options(_core PRIVATE "-Wl,-exported_symbol,_PyInit__core")
endif()
install(TARGETS _core LIBRARY DESTINATION fosu RUNTIME DESTINATION fosu)
