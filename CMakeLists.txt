cmake_minimum_required(VERSION 3.20)
project(vanadium VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

if(MSVC)
  add_compile_options(/W3 /utf-8)
else()
  add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

# ---- generated code library (unicode tables, catalogs, self-test vectors) ----
add_subdirectory(src/gen)

# ---- kernel core: platform, json, crypto, ui, i18n, settings, store, remote ----
set(VD_CORE_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/src/core/core.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/core/crypto.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/core/ui.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/core/i18n.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/core/settings.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/core/store.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/core/remote/http.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/core/remote/winhttp_util.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/core/remote/download.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/core/remote/github.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/core/remote/auth.cpp")

add_library(vd_core STATIC ${VD_CORE_SOURCES})
target_include_directories(vd_core PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/gen")
target_link_libraries(vd_core PUBLIC vd_gen)
if(WIN32)
  target_link_libraries(vd_core PUBLIC winhttp)
endif()

# ---- command layer ----
file(GLOB VD_COMMAND_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/commands/*.cpp")
add_library(vd_commands STATIC ${VD_COMMAND_SOURCES})
target_link_libraries(vd_commands PUBLIC vd_core)

# ---- the kernel DLL loaded by the Rust shell ----
add_library(vanadium SHARED "${CMAKE_CURRENT_SOURCE_DIR}/src/core/abi.cpp")
target_link_libraries(vanadium PRIVATE vd_commands vd_core vd_gen)
set_target_properties(vanadium PROPERTIES
  PREFIX ""
  OUTPUT_NAME "vanadium"
)
if(WIN32)
  target_link_options(vanadium PRIVATE -static-libgcc -static-libstdc++)
endif()
add_custom_command(TARGET vanadium POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_SOURCE_DIR}/dist"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:vanadium>" "${CMAKE_CURRENT_SOURCE_DIR}/dist/vanadium.dll"
  COMMENT "Copying vanadium.dll to dist/")

# ---- self tests ----
foreach(t t_crypto t_store)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/${t}.cpp")
    add_executable(${t} "${CMAKE_CURRENT_SOURCE_DIR}/tests/${t}.cpp")
    target_link_libraries(${t} PRIVATE vd_core)
    set_target_properties(${t} PROPERTIES RUNTIME_OUTPUT_${CMAKE_CURRENT_SOURCE_DIR}ECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build")
  endif()
endforeach()

# ---- remote probe (standalone: brings its own shim, never linked into the DLL) ----
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/core/remote/probe.cpp")
  add_executable(vd_probe
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/remote/probe.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/remote/probe_shim.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/remote/http.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/remote/winhttp_util.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/remote/download.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/remote/github.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/remote/auth.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/crypto.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gen/tables_00.cpp")
  target_include_directories(vd_probe PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gen")
  if(WIN32)
    target_link_libraries(vd_probe PRIVATE winhttp)
  endif()
  set_target_properties(vd_probe PROPERTIES RUNTIME_OUTPUT_${CMAKE_CURRENT_SOURCE_DIR}ECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build")
endif()
