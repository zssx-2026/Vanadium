cmake_minimum_required(VERSION 3.20)
project(vanadium VERSION 0.4.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

if(MSVC)
  add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:/W3> $<$<COMPILE_LANGUAGE:C,CXX>:/utf-8>)
else()
  # Scope warnings to C/C++ so the NASM step does not receive -Wall/-Wextra
  # (nasm rejects them as unknown warning names).
  add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:-Wall> $<$<COMPILE_LANGUAGE:C,CXX>:-Wextra>
                      $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-unused-parameter>)
endif()

# ---- optional NASM acceleration (CRC-64 slicing-by-8) ----
set(VD_HAVE_CRC64_ASM OFF)
find_program(NASM_EXECUTABLE NAMES nasm)
if(NASM_EXECUTABLE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/asm/crc64.asm")
  enable_language(ASM_NASM)
  set(CMAKE_ASM_NASM_OBJECT_FORMAT win64)
  set(VD_HAVE_CRC64_ASM ON)
  add_library(vd_asm OBJECT "${CMAKE_CURRENT_SOURCE_DIR}/src/asm/crc64.asm")
  target_compile_options(vd_asm PRIVATE -f win64 -w-reloc-rel-dword)
  message(STATUS "NASM found: ${NASM_EXECUTABLE} (CRC-64 assembly enabled)")
else()
  message(STATUS "NASM not found: CRC-64 uses the portable C++ path")
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
  "${CMAKE_CURRENT_SOURCE_DIR}/src/core/registry.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/core/remote/http.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/core/remote/transport.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/core/remote/net/uri.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/core/remote/net/socket.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/core/remote/net/tls.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/core/remote/net/http1.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/core/remote/net/sched.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/core/remote/download.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/core/remote/github.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/core/remote/auth.cpp")

add_library(vd_core STATIC ${VD_CORE_SOURCES} $<TARGET_OBJECTS:vd_asm>)
target_include_directories(vd_core PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/gen")
target_link_libraries(vd_core PUBLIC vd_gen)
if(VD_HAVE_CRC64_ASM)
  target_compile_definitions(vd_core PUBLIC VD_HAVE_CRC64_ASM=1)
endif()
if(WIN32)
  # Native transport: Winsock for TCP/DNS, Schannel (secur32) for TLS, crypt32
  # for certificate chain validation. No WinHTTP.
  target_link_libraries(vd_core PUBLIC ws2_32 crypt32 secur32)
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
    set_target_properties(${t} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build")
  endif()
endforeach()

# ---- remote probe (standalone: brings its own shim, never linked into the DLL) ----
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/core/remote/probe.cpp")
  add_executable(vd_probe
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/remote/probe.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/remote/probe_shim.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/remote/http.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/remote/transport.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/remote/net/uri.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/remote/net/socket.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/remote/net/tls.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/remote/net/http1.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/remote/net/sched.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/remote/download.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/remote/github.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/remote/auth.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/crypto.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gen/tables_00.cpp")
  target_include_directories(vd_probe PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gen")
  if(WIN32)
    target_link_libraries(vd_probe PRIVATE ws2_32 crypt32 secur32)
  endif()
  set_target_properties(vd_probe PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build")
endif()
