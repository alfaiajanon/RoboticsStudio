# WAMR (extern/wamr) as the static library `vmlib`, configured for the robot's
# firmware: fast interpreter, thread manager (so the watchdog's
# wasm_runtime_terminate stops busy loops), no WASI from WAMR (the simulator
# serves its own), reference types + bulk memory (clang's default wasm
# features). Its own directory keeps runtime_lib.cmake's add_definitions out
# of the app. See temp/progress.md, Phase 4 spike, for why each flag is set.
set(WAMR_ROOT_DIR ${CMAKE_CURRENT_LIST_DIR}/../../extern/wamr)

set(WAMR_BUILD_PLATFORM linux)
set(WAMR_BUILD_TARGET X86_64)
set(WAMR_BUILD_INTERP 1)
set(WAMR_BUILD_FAST_INTERP 1)
set(WAMR_BUILD_AOT 0)
set(WAMR_BUILD_JIT 0)
set(WAMR_BUILD_FAST_JIT 0)
set(WAMR_BUILD_SIMD 0)
set(WAMR_BUILD_LIBC_BUILTIN 1)
set(WAMR_BUILD_LIBC_WASI 0)
set(WAMR_BUILD_THREAD_MGR 1)
set(WAMR_BUILD_REF_TYPES 1)
set(WAMR_BUILD_BULK_MEMORY 1)
set(WAMR_BUILD_DUMP_CALL_STACK 1)
set(WAMR_BUILD_CUSTOM_NAME_SECTION 1)   # function names in crash stacks
# WAMR prints through rs_wamr_vprintf (wamr_print.c), which drops it.
set(WAMR_BH_VPRINTF rs_wamr_vprintf)

# The interpreter stays optimised in Debug builds of the app.
set(CMAKE_C_FLAGS_DEBUG "-O2 -g")

include(${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake)
add_library(vmlib STATIC ${WAMR_RUNTIME_LIB_SOURCE} ${CMAKE_CURRENT_LIST_DIR}/wamr_print.c)
set_target_properties(vmlib PROPERTIES POSITION_INDEPENDENT_CODE ON AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_include_directories(vmlib PUBLIC ${WAMR_ROOT_DIR}/core/iwasm/include)
target_link_libraries(vmlib PUBLIC dl pthread m)
