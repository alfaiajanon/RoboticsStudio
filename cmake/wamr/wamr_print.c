/*
 * WAMR's own printing (os_printf), redirected here by WAMR_BH_VPRINTF.
 * Discarded: WAMR prints a call stack on every exception, including the
 * simulator's own terminate on Stop. Everything the user needs reaches the
 * Output panel another way: load errors through the error buffer, crashes
 * through the exception text and wasm_runtime_dump_call_stack_to_buf
 * (FirmwareRuntime).
 */
#include <stdarg.h>

int rs_wamr_vprintf(const char* format, va_list ap) {
    (void)format;
    (void)ap;
    return 0;
}
