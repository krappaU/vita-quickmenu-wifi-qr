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

# Functions SceShell exports to its plugins (stubs/SceShell.yml), linked weakly.
set(SHELL_STUBS ${CMAKE_BINARY_DIR}/shell_stubs)
add_custom_command(
  OUTPUT ${SHELL_STUBS}/libSceShell_stub_weak.a
  COMMAND ${CMAKE_COMMAND} -E make_directory ${SHELL_STUBS}
  COMMAND vita-libs-gen ${CMAKE_SOURCE_DIR}/stubs/SceShell.yml ${SHELL_STUBS}
  COMMAND make -C ${SHELL_STUBS} -s
  DEPENDS ${CMAKE_SOURCE_DIR}/stubs/SceShell.yml)
add_custom_target(shell_stubs DEPENDS ${SHELL_STUBS}/libSceShell_stub_weak.a)

# The two PAF functions the Quick Menu button uses (stubs/ScePafToplevel.yml), linked weakly.
set(PAF_STUBS ${CMAKE_BINARY_DIR}/paf_stubs)
add_custom_command(
  OUTPUT ${PAF_STUBS}/libScePafToplevel_stub_weak.a
  COMMAND ${CMAKE_COMMAND} -E make_directory ${PAF_STUBS}
  COMMAND vita-libs-gen ${CMAKE_SOURCE_DIR}/stubs/ScePafToplevel.yml ${PAF_STUBS}
  COMMAND make -C ${PAF_STUBS} -s
  DEPENDS ${CMAKE_SOURCE_DIR}/stubs/ScePafToplevel.yml)
add_custom_target(paf_stubs DEPENDS ${PAF_STUBS}/libScePafToplevel_stub_weak.a)

# The header is included as <quickmenureborn/qm_reborn.h>.
set(QMR_INC ${CMAKE_BINARY_DIR}/qmr_include)
file(MAKE_DIRECTORY ${QMR_INC})
file(COPY ${TP}/QuickMenuReborn/src/quickmenureborn DESTINATION ${QMR_INC})

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -Wall -Wextra -Wno-unused-parameter -fno-builtin-printf -fno-math-errno -DNDEBUG")
add_compile_definitions(QUIRC_FLOAT_TYPE=float QUIRC_USE_TGMATH)

# Loader: wifiqr.suprx, under *main in taiHEN's config.txt. Loads the core once the shell has
# started (src/loader.c says why).
add_executable(wifiqr_loader src/loader.c)
target_link_options(wifiqr_loader PRIVATE -nostdlib)
target_link_libraries(wifiqr_loader SceIofilemgr_stub SceKernelThreadMgr_stub SceLibKernel_stub SceKernelModulemgr_stub gcc)
vita_create_self(wifiqr.suprx wifiqr_loader CONFIG ${CMAKE_SOURCE_DIR}/loader.yml UNSAFE)

# Core: wifiqr_core.suprx, loaded into SceShell by the loader. Scanning, saving, connecting, and
# the API front ends call (src/wifiqr_api.h).
add_executable(wifiqr
  src/core.c
  src/connect.c
  src/libc_shim.c
  src/log.c
  src/profiles.c
  src/qr_decode.c
  src/reg_vita.c
  src/scan.c
  src/selftest.c
  src/viewfinder.c
  src/wifi_qr.c
  ${TP}/quirc/lib/decode.c
  src/quirc_ext.c  # includes quirc's identify.c
  ${TP}/quirc/lib/quirc.c
  ${TP}/quirc/lib/version_db.c
)
add_dependencies(wifiqr shell_stubs wifiqr_kernel_stubs)
target_include_directories(wifiqr PRIVATE src kernel ${TP}/quirc/lib)
# quirc's own warnings are not ours to fix
set_source_files_properties(${TP}/quirc/lib/decode.c src/quirc_ext.c ${TP}/quirc/lib/quirc.c
  ${TP}/quirc/lib/version_db.c PROPERTIES COMPILE_FLAGS "-w")

target_link_directories(wifiqr PRIVATE ${SHELL_STUBS} ${CMAKE_BINARY_DIR}/wifiqr_kernel_stubs)
target_link_options(wifiqr PRIVATE -nostdlib)
target_link_libraries(wifiqr
  WifiQrKernel_stub_weak  # optional: without the kernel plugin, its calls just fail
  SceShell_stub_weak
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
vita_create_self(wifiqr_core.suprx wifiqr CONFIG ${CMAKE_SOURCE_DIR}/core.yml UNSAFE)

# The NIDs front ends look the API up by (src/wifiqr_api.h) must match the core's exports.
add_custom_command(TARGET wifiqr POST_BUILD
  COMMAND vita-elf-export u $<TARGET_FILE:wifiqr> ${CMAKE_SOURCE_DIR}/core.yml ${CMAKE_BINARY_DIR}/core_exports.yml
  COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check_nids.py ${CMAKE_BINARY_DIR}/core_exports.yml
          ${CMAKE_SOURCE_DIR}/src/wifiqr_api.h
  VERBATIM)

# Quick Menu button: wifiqr_qm.suprx, loaded by QuickMenuReborn. Only widgets; calls the core.
add_executable(wifiqr_qm src/qm.c src/log.c)
add_dependencies(wifiqr_qm qmr_stubs paf_stubs)
target_include_directories(wifiqr_qm PRIVATE src ${QMR_INC})
target_link_directories(wifiqr_qm PRIVATE ${QMR_STUBS} ${PAF_STUBS})
target_link_options(wifiqr_qm PRIVATE -nostdlib)
target_link_libraries(wifiqr_qm
  QuickMenuReborn_stub
  ScePafToplevel_stub_weak
  taihen_stub
  SceIofilemgr_stub
  SceKernelThreadMgr_stub
  SceLibKernel_stub
  SceRtc_stub
  SceKernelModulemgr_stub
  gcc
)
vita_create_self(wifiqr_qm.suprx wifiqr_qm CONFIG ${CMAKE_SOURCE_DIR}/qm.yml UNSAFE)

# Kernel helper: draws the camera preview over SceShell's frames (see kernel/main.c).
add_executable(wifiqr_kernel kernel/main.c)
target_link_options(wifiqr_kernel PRIVATE -nostdlib)
target_link_libraries(wifiqr_kernel
  taihenForKernel_stub
  SceDisplayForDriver_stub
  SceSysclibForDriver_stub
  SceSysmemForDriver_stub
  SceThreadmgrForDriver_stub
  SceDebugForDriver_stub
  gcc
)
vita_create_self(wifiqr_kernel.skprx wifiqr_kernel CONFIG ${CMAKE_SOURCE_DIR}/kernel/exports.yml UNSAFE)
vita_create_stubs(wifiqr_kernel_stubs wifiqr_kernel ${CMAKE_SOURCE_DIR}/kernel/exports.yml KERNEL)
