/* test_escape.c --- ANSI/SS3 decoder: whole-sequence consumption, no injection */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include "elib_cli.h"

#define RX_SIZE     32u
#define HIST_SIZE   64u
#define TABLE_SIZE  4u
#define OUT_SIZE    512u

static elib_cli_ctx_t ctx;
static char rx[RX_SIZE];
static char hist[HIST_SIZE];
static elib_cli_cmd_t table[TABLE_SIZE];
static char out[OUT_SIZE];
static size_t out_len;
static unsigned calls;

static int capture(void *ud, const char *fmt, ...)
{
    va_list ap;
    int n;

    (void)ud;
    va_start(ap, fmt);
    n = vsnprintf(out + out_len, OUT_SIZE - out_len, fmt, ap);
    va_end(ap);
    if (n > 0) {
        out_len += (size_t)n;
        if (out_len >= OUT_SIZE) {
            out_len = OUT_SIZE - 1u;
        }
    }
    return n;
}

static void on_cmd(void *ud, const char *args)
{
    (void)ud;
    (void)args;
    calls++;
}

static void reset(elib_cli_nl_t nl)
{
    elib_cli_cfg_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    memset(rx, 0, sizeof(rx));
    memset(hist, 0, sizeof(hist));
    memset(table, 0, sizeof(table));
    memset(&ctx, 0, sizeof(ctx));
    out_len = 0u;
    out[0] = '\0';
    calls = 0u;

    cfg.prompt = "> ";
    cfg.newline = nl;
    cfg.print = capture;
    cfg.rx_buf = rx;
    cfg.rx_buf_size = RX_SIZE;
    cfg.history_buf = hist;
    cfg.history_buf_size = HIST_SIZE;
    cfg.cmd_table = table;
    cfg.cmd_table_size = TABLE_SIZE;
    cfg.flags = ELIB_CLI_FLAG_ECHO;
    assert(elib_cli_init(&ctx, &cfg) == ELIB_CLI_OK);
    assert(elib_cli_register(&ctx, "aaa", NULL, on_cmd) == ELIB_CLI_OK);
    assert(elib_cli_register(&ctx, "bbb", NULL, on_cmd) == ELIB_CLI_OK);
}

static void feed(const char *s)
{
    while (*s != '\0') {
        (void)elib_cli_feed_char(&ctx, *s);
        s++;
    }
}

static void out_clear(void)
{
    out_len = 0u;
    out[0] = '\0';
}

static void test_csi_cursor_up(void)
{
    printf("Test: ESC [ A recalls the previous entry... ");
    reset(ELIB_CLI_NL_LF);
    feed("aaa\nbbb\n");
    out_clear();
    feed("\x1b[A");
    assert(strcmp(rx, "bbb") == 0);
    assert(strcmp(out, "bbb") == 0);
    printf("PASSED\n");
}

static void test_csi_cursor_down(void)
{
    printf("Test: ESC [ B moves forward again... ");
    reset(ELIB_CLI_NL_LF);
    feed("aaa\nbbb\n");
    feed("\x1b[A");
    feed("\x1b[A");
    assert(strcmp(rx, "aaa") == 0);
    feed("\x1b[B");
    assert(strcmp(rx, "bbb") == 0);
    printf("PASSED\n");
}

static void test_ss3_cursor_keys(void)
{
    printf("Test: ESC O A / ESC O B work like their CSI forms... ");
    reset(ELIB_CLI_NL_LF);
    feed("aaa\nbbb\n");
    feed("\x1bOA");
    assert(strcmp(rx, "bbb") == 0);
    feed("\x1bOB");
    assert(ctx.hist_offset == 0);
    printf("PASSED\n");
}

static void test_parameterised_up(void)
{
    printf("Test: ESC [ 1 ; 5 A neither injects nor breaks... ");
    reset(ELIB_CLI_NL_LF);
    feed("aaa\n");
    out_clear();
    feed("\x1b[1;5A");
    assert(strcmp(rx, "aaa") == 0);        /* no "1;5" reached the line */
    assert(strcmp(out, "aaa") == 0);
    printf("PASSED\n");
}

static void test_other_sequences_are_swallowed(void)
{
    printf("Test: unrelated escape sequences are consumed whole... ");
    reset(ELIB_CLI_NL_LF);
    feed("ab");
    feed("\x1b[2J");
    feed("cd");
    assert(strcmp(rx, "abcd") == 0);
    assert(strcmp(out, "abcd") == 0);
    printf("PASSED\n");
}

static void test_delete_key_is_swallowed(void)
{
    printf("Test: ESC [ 3 ~ swallows its tilde... ");
    reset(ELIB_CLI_NL_LF);
    feed("ab");
    feed("\x1b[3~");
    feed("c");
    assert(strcmp(rx, "abc") == 0);
    assert(strcmp(out, "abc") == 0);
    printf("PASSED\n");
}

static void test_stray_esc_swallows_one_byte(void)
{
    printf("Test: a stray ESC swallows exactly one byte... ");
    reset(ELIB_CLI_NL_LF);
    feed("ab");
    feed("\x1bx");
    feed("cd");
    assert(strcmp(rx, "abcd") == 0);
    assert(strcmp(out, "abcd") == 0);
    printf("PASSED\n");
}

static void test_stray_esc_at_end(void)
{
    printf("Test: a trailing ESC leaves the decoder waiting... ");
    reset(ELIB_CLI_NL_LF);
    feed("ab");
    feed("\x1b");
    assert(strcmp(rx, "ab") == 0);
    assert(ctx.escape_state != 0u);
    feed("c");                             /* consumed as a one-byte sequence */
    assert(strcmp(rx, "ab") == 0);
    assert(ctx.escape_state == 0u);
    feed("d");
    assert(strcmp(rx, "abd") == 0);
    printf("PASSED\n");
}

static void test_csi_without_final(void)
{
    printf("Test: an unfinished CSI still owns the next byte... ");
    reset(ELIB_CLI_NL_LF);
    feed("ab\x1b[");
    assert(strcmp(rx, "ab") == 0);
    feed("x");                             /* 'x' is the final byte */
    assert(strcmp(rx, "ab") == 0);
    feed("y");
    assert(strcmp(rx, "aby") == 0);
    printf("PASSED\n");
}

static void test_intermediate_bytes_are_inside_the_body(void)
{
    printf("Test: intermediate bytes stay inside the sequence... ");
    reset(ELIB_CLI_NL_LF);
    feed("ab\x1b[2 q");
    assert(strcmp(rx, "ab") == 0);         /* ' ' is intermediate, 'q' is final */
    printf("PASSED\n");
}

static void test_escape_hides_cr_from_the_terminator(void)
{
    printf("Test: a CR inside an escape sequence never submits... ");
    reset(ELIB_CLI_NL_CR);
    feed("\x1b\r");
    assert(calls == 0u);
    assert(out_len == 0u);
    assert(strcmp(rx, "") == 0);
    printf("PASSED\n");
}

static void test_escape_hides_lf_from_the_terminator(void)
{
    printf("Test: an LF inside an escape sequence never submits... ");
    reset(ELIB_CLI_NL_LF);
    feed("\x1b\n");
    assert(calls == 0u);
    assert(out_len == 0u);
    printf("PASSED\n");
}

static void test_escape_hides_crlf_pair(void)
{
    printf("Test: an escape swallows CR and the LF after it can submit later... ");
    reset(ELIB_CLI_NL_CRLF);
    feed("\x1b\r");
    assert(calls == 0u);
    assert(out_len == 0u);
    feed("\n");                            /* lone LF is ignored in CRLF mode */
    assert(calls == 0u);
    assert(out_len == 0u);
    printf("PASSED\n");
}

static void test_escape_before_newline(void)
{
    printf("Test: a byte swallowed by ESC cannot submit the line... ");
    reset(ELIB_CLI_NL_LF);
    feed("ab\x1b\n");
    assert(calls == 0u);
    feed("c");
    assert(strcmp(rx, "abc") == 0);
    printf("PASSED\n");
}

static void test_nul_terminates_escape(void)
{
    printf("Test: a NUL byte ends an escape sequence... ");
    reset(ELIB_CLI_NL_LF);
    feed("ab");
    assert(elib_cli_feed_char(&ctx, 0x1b) == ELIB_CLI_OK);
    assert(elib_cli_feed_char(&ctx, 0x00) == ELIB_CLI_OK);
    assert(ctx.escape_state == 0u);
    feed("c");
    assert(strcmp(rx, "abc") == 0);
    printf("PASSED\n");
}

static void test_ss3_function_key_is_swallowed(void)
{
    printf("Test: SS3 function keys are swallowed without side effects... ");
    reset(ELIB_CLI_NL_LF);
    feed("ab");
    feed("\x1bOP");                        /* F1 */
    feed("c");
    assert(strcmp(rx, "abc") == 0);
    printf("PASSED\n");
}

static void test_escape_on_empty_history(void)
{
    printf("Test: cursor keys with no history do nothing... ");
    reset(ELIB_CLI_NL_LF);
    feed("\x1b[A\x1b[B");
    assert(ctx.rx_pos == 0u);
    assert(out_len == 0u);
    assert(ctx.hist_offset == 0);
    printf("PASSED\n");
}

int main(void)
{
    printf("=== test_escape ===\n");
    test_csi_cursor_up();
    test_csi_cursor_down();
    test_ss3_cursor_keys();
    test_parameterised_up();
    test_other_sequences_are_swallowed();
    test_delete_key_is_swallowed();
    test_stray_esc_swallows_one_byte();
    test_stray_esc_at_end();
    test_csi_without_final();
    test_intermediate_bytes_are_inside_the_body();
    test_escape_hides_cr_from_the_terminator();
    test_escape_hides_lf_from_the_terminator();
    test_escape_hides_crlf_pair();
    test_escape_before_newline();
    test_nul_terminates_escape();
    test_ss3_function_key_is_swallowed();
    test_escape_on_empty_history();
    printf("=== test_escape: all PASSED ===\n");
    return 0;
}
