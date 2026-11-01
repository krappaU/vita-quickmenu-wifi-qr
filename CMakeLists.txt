cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

project(wifiqr C)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(TP ${CMAKE_SOURCE_DIR}/third_party)
if(NOT EXISTS ${TP}/QuickMenuReborn/QuickMenuReborn.yml OR NOT EXISTS ${TP}/quirc/lib/quirc.c)
  message(FATAL_ERROR "Run scripts/fetch_deps.sh first")
endif()

# QuickMenuReborn's link stubs, generated from its NID table (the same file its release's
# dev_pkg.zip stubs are built from).
set(QMR_STUBS ${CMAKE_BINARY_DIR}/qmr_stubs)
add_custom_command(
  OUTPUT ${QMR_STUBS}/libQuickMenuReborn_stub.a
  COMMAND ${CMAKE_COMMAND} -E make_directory ${QMR_STUBS}
  COMMAND vita-libs-gen ${TP}/QuickMenuReborn/QuickMenuReborn.yml ${QMR_STUBS}
  COMMAND make -C ${QMR_STUBS} -s
  DEPENDS ${TP}/QuickMenuReborn/QuickMenuReborn.yml)
add_custom_target(qmr_stubs DEPENDS ${QMR_STUBS}/libQuickMenuReborn_stub.a)

# The header is included as <quickmenureborn/qm_reborn.h>.
set(QMR_INC ${CMAKE_BINARY_DIR}/qmr_include)
file(MAKE_DIRECTORY ${QMR_INC})
file(COPY ${TP}/QuickMenuReborn/src/quickmenureborn DESTINATION ${QMR_INC})

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -Wall -Wextra -Wno-unused-parameter -fno-builtin-printf -fno-math-errno -DNDEBUG")
add_compile_definitions(QUIRC_FLOAT_TYPE=float QUIRC_USE_TGMATH)

add_executable(wifiqr
  src/main.c
  src/connect.c
  src/libc_shim.c
  src/log.c
  src/profiles.c
  src/qr_decode.c
  src/reg_vita.c
  src/scan.c
  src/wifi_qr.c
  ${TP}/quirc/lib/decode.c
  ${TP}/quirc/lib/identify.c
  ${TP}/quirc/lib/quirc.c
  ${TP}/quirc/lib/version_db.c
)
add_dependencies(wifiqr qmr_stubs)
target_include_directories(wifiqr PRIVATE src ${TP}/quirc/lib ${QMR_INC})
# quirc's own warnings are not ours to fix
set_source_files_properties(${TP}/quirc/lib/decode.c ${TP}/quirc/lib/identify.c ${TP}/quirc/lib/quirc.c
  ${TP}/quirc/lib/version_db.c PROPERTIES COMPILE_FLAGS "-w")

target_link_directories(wifiqr PRIVATE ${QMR_STUBS})
target_link_options(wifiqr PRIVATE -nostdlib)
target_link_libraries(wifiqr
  QuickMenuReborn_stub
  SceCamera_stub
  SceIofilemgr_stub
  SceKernelThreadMgr_stub
  SceLibKernel_stub
  SceNetCtl_stub
  SceNet_stub
  SceRegistryMgr_stub
  SceRtc_stub
  SceSysmem_stub
  gcc
)

vita_create_self(wifiqr.suprx wifiqr CONFIG ${CMAKE_SOURCE_DIR}/exports.yml UNSAFE)
