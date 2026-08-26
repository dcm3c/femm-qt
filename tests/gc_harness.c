#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "py/compile.h"
#include "py/runtime.h"
#include "py/gc.h"
#include "py/stackctrl.h"
#include "py/mphal.h"

static char s_heap[2 * 1024 * 1024];

mp_uint_t mp_hal_stdout_tx_strn(const char *str, size_t len) { fwrite(str, 1, len, stdout); return len; }
void mp_hal_stdout_tx_strn_cooked(const char *str, size_t len) { fwrite(str, 1, len, stdout); }
int mp_hal_stdin_rx_chr(void) { return -1; }

static void run(const char *src) {
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_lexer_t *lex = mp_lexer_new_from_str_len(MP_QSTR__lt_stdin_gt_, src, strlen(src), 0);
        qstr source_name = lex->source_name;
        mp_parse_tree_t parse_tree = mp_parse(lex, MP_PARSE_FILE_INPUT);
        mp_obj_t module_fun = mp_compile(&parse_tree, source_name, true);
        mp_call_function_0(module_fun);
        nlr_pop();
    } else {
        mp_obj_print_exception(&mp_plat_print, (mp_obj_t)nlr.ret_val);
    }
}

static double now(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec * 1e-9; }

int main(int argc, char **argv) {
    mp_stack_ctrl_init();
    mp_stack_set_limit(8 * 1024 * 1024);
    gc_init(s_heap, s_heap + sizeof(s_heap));
    mp_init();
    if (argc > 1) {
        FILE *f = fopen(argv[1], "rb");
        fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
        char *buf = malloc(n + 1); fread(buf, 1, n, f); buf[n] = 0; fclose(f);
        double t0 = now();
        run(buf);
        printf("[script wall %.3f s]\n", now() - t0);
    }
    gc_info_t info; gc_info(&info);
    int nareas = 0;
    for (mp_state_mem_area_t *a = &MP_STATE_MEM(area); a; a = a->next) nareas++;
    printf("[gc total %zu bytes, used %zu, areas %d]\n", (size_t)info.total, (size_t)info.used, nareas);
    mp_deinit();
    return 0;
}
