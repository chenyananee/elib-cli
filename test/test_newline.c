/* test_newline.c --- LF / CR / CRLF submission and the emitted terminator */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include "elib_cli.h"

#define RX_SIZE     32u
#define HIST_SIZE   64u
#define TABLE_SIZE  4u
#define OUT_SIZE    256u

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
    assert(elib_cli_register(&ctx, "led", NULL, on_cmd) == ELIB_CLI_OK);
}

static void feed(const char *s)
{
    while (*s != '\0') {
        (void)elib_cli_feed_char(&ctx, *s);
        s++;
    }
}

static void test_lf_submits_on_lf(void)
{
    printf("Test: LF mode submits on '\\n' and emits '\\n'... ");
    reset(ELIB_CLI_NL_LF);
    feed("led\n");
    assert(calls == 1u);
    assert(strcmp(out, "led\n> ") == 0);
    printf("PASSED\n");
}

static void test_lf_ignores_cr(void)
{
    printf("Test: LF mode ignores '\\r'... ");
    reset(ELIB_CLI_NL_LF);
    feed("led\r\n");
    assert(calls == 1u);
    assert(strcmp(out, "led\n> ") == 0);   /* the CR is neither echoed nor kept */
    printf("PASSED\n");
}

static void test_cr_submits_on_cr(void)
{
    printf("Test: CR mode submits on '\\r' and emits '\\r'... ");
    reset(ELIB_CLI_NL_CR);
    feed("led\r");
    assert(calls == 1u);
    assert(strcmp(out, "led\r> ") == 0);
    printf("PASSED\n");
}

static void test_cr_ignores_lf(void)
{
    printf("Test: CR mode ignores '\\n'... ");
    reset(ELIB_CLI_NL_CR);
    feed("led\n");
    assert(calls == 0u);
    assert(strcmp(out, "led") == 0);
    assert(strcmp(rx, "led") == 0);
    printf("PASSED\n");
}

static void test_crlf_pair_submits_once(void)
{
    printf("Test: CRLF mode submits once per \"\\r\\n\" and emits it... ");
    reset(ELIB_CLI_NL_CRLF);
    feed("led\r\n");
    assert(calls == 1u);
    assert(strcmp(out, "led\r\n> ") == 0);
    printf("PASSED\n");
}

static void test_crlf_lone_cr_submits_immediately(void)
{
    printf("Test: CRLF mode submits on a lone '\\r' at once... ");
    reset(ELIB_CLI_NL_CRLF);
    feed("led\r");
    /* The prompt must be there before any further byte arrives, otherwise a
     * terminal that only sends CR looks dead. */
    assert(calls == 1u);
    assert(strcmp(out, "led\r\n> ") == 0);
    printf("PASSED\n");
}

static void test_crlf_swallows_the_partner(void)
{
    printf("Test: CRLF mode swallows exactly one '\\n' after the '\\r'... ");
    reset(ELIB_CLI_NL_CRLF);
    feed("led\r\n\n\n");
    assert(calls == 1u);                   /* no extra submits from the LFs */
    assert(strcmp(out, "led\r\n> ") == 0);
    printf("PASSED\n");
}

static void test_crlf_keeps_the_byte_after_lone_cr(void)
{
    printf("Test: CRLF mode keeps a byte that follows a lone '\\r'... ");
    reset(ELIB_CLI_NL_CRLF);
    feed("led\rX");
    assert(calls == 1u);
    assert(strcmp(out, "led\r\n> X") == 0);
    assert(strcmp(rx, "X") == 0);
    printf("PASSED\n");
}

static void test_crlf_two_lone_crs(void)
{
    printf("Test: CRLF mode treats two CRs as two submits... ");
    reset(ELIB_CLI_NL_CRLF);
    feed("led\r\r");
    assert(calls == 1u);                   /* the second submit is an empty line */
    assert(strcmp(out, "led\r\n> \r\n> ") == 0);
    printf("PASSED\n");
}

static void test_crlf_lone_lf_is_ignored(void)
{
    printf("Test: CRLF mode ignores a lone '\\n'... ");
    reset(ELIB_CLI_NL_CRLF);
    feed("led\n");
    assert(calls == 0u);
    assert(strcmp(rx, "led") == 0);
    assert(strcmp(out, "led") == 0);
    printf("PASSED\n");
}

static void test_terminator_matches_mode(void)
{
    printf("Test: the emitted terminator follows the configured mode... ");
    reset(ELIB_CLI_NL_LF);
    feed("\n");
    assert(strcmp(out, "\n> ") == 0);

    reset(ELIB_CLI_NL_CR);
    feed("\r");
    assert(strcmp(out, "\r> ") == 0);

    reset(ELIB_CLI_NL_CRLF);
    feed("\r\n");
    assert(strcmp(out, "\r\n> ") == 0);
    printf("PASSED\n");
}

static void test_consecutive_submits(void)
{
    printf("Test: consecutive terminators submit consecutive lines... ");
    reset(ELIB_CLI_NL_LF);
    feed("\n\n");
    assert(calls == 0u);
    assert(strcmp(out, "\n> \n> ") == 0);

    reset(ELIB_CLI_NL_LF);
    feed("led\nled\n");
    assert(calls == 2u);
    assert(strcmp(out, "led\n> led\n> ") == 0);
    printf("PASSED\n");
}

static void test_newline_does_not_enter_the_line(void)
{
    printf("Test: terminators are never stored in the line... ");
    reset(ELIB_CLI_NL_CRLF);
    feed("led\r\n");
    assert(ctx.rx_pos == 0u);
    assert(rx[0] == '\0');
    assert(strcmp(hist, "led\n") == 0);     /* history always uses '\n' */
    printf("PASSED\n");
}

static void test_cr_mode_second_cr_after_lf(void)
{
    printf("Test: CR mode submits on every CR even after an ignored LF... ");
    reset(ELIB_CLI_NL_CR);
    feed("led\n\r");
    assert(calls == 1u);
    assert(strcmp(out, "led\r> ") == 0);
    printf("PASSED\n");
}

static void test_switch_mode_by_reinit(void)
{
    printf("Test: re-init changes the active terminator... ");
    reset(ELIB_CLI_NL_LF);
    feed("led\n");
    assert(calls == 1u);

    reset(ELIB_CLI_NL_CR);
    feed("led\r");
    assert(calls == 1u);
    assert(strcmp(out, "led\r> ") == 0);
    printf("PASSED\n");
}

int main(void)
{
    printf("=== test_newline ===\n");
    test_lf_submits_on_lf();
    test_lf_ignores_cr();
    test_cr_submits_on_cr();
    test_cr_ignores_lf();
    test_crlf_pair_submits_once();
    test_crlf_lone_cr_submits_immediately();
    test_crlf_swallows_the_partner();
    test_crlf_keeps_the_byte_after_lone_cr();
    test_crlf_two_lone_crs();
    test_crlf_lone_lf_is_ignored();
    test_terminator_matches_mode();
    test_consecutive_submits();
    test_newline_does_not_enter_the_line();
    test_cr_mode_second_cr_after_lf();
    test_switch_mode_by_reinit();
    printf("=== test_newline: all PASSED ===\n");
    return 0;
}
