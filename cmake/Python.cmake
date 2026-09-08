find_package(Python REQUIRED COMPONENTS Interpreter Development.SABIModule)
set(generated "${CMAKE_CURRENT_BINARY_DIR}/_core.cc")
add_custom_command(OUTPUT "${generated}"
  COMMAND Python::Interpreter "${PROJECT_SOURCE_DIR}/python/build_ffi.py" "${generated}"
  DEPENDS python/build_ffi.py src/fosu/bindings/c_api.h VERBATIM)
Python_add_library(_core MODULE USE_SABI 3.10 WITH_SOABI "${generated}" src/fosu/bindings/c_api.cc)
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
install(TARGETS _core LIBRARY DESTINATION fosu)
