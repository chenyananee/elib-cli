/* test_lifecycle.c --- deinit gating, revival, independent contexts */
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

static elib_cli_cfg_t make_cfg(char *rxbuf, uint32_t rxsize, char *histbuf,
                               elib_cli_cmd_t *tbl, uint32_t tblsize,
                               const char *prompt)
{
    elib_cli_cfg_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.prompt = prompt;
    cfg.newline = ELIB_CLI_NL_LF;
    cfg.print = capture;
    cfg.rx_buf = rxbuf;
    cfg.rx_buf_size = rxsize;
    cfg.history_buf = histbuf;
    cfg.history_buf_size = HIST_SIZE;
    cfg.cmd_table = tbl;
    cfg.cmd_table_size = tblsize;
    cfg.flags = ELIB_CLI_FLAG_ECHO;
    return cfg;
}

static void reset(void)
{
    elib_cli_cfg_t cfg;

    memset(rx, 0, sizeof(rx));
    memset(hist, 0, sizeof(hist));
    memset(table, 0, sizeof(table));
    memset(&ctx, 0, sizeof(ctx));
    out_len = 0u;
    out[0] = '\0';
    calls = 0u;

    cfg = make_cfg(rx, RX_SIZE, hist, table, TABLE_SIZE, "> ");
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

static void test_deinit_is_null_safe(void)
{
    printf("Test: deinit(NULL) does nothing... ");
    elib_cli_deinit(NULL);
    printf("PASSED\n");
}

static void test_deinit_clears_the_state(void)
{
    printf("Test: deinit clears every runtime flag... ");
    reset();
    feed("led");
    feed("\x1b[");
    ctx.hist_offset = 1;

    elib_cli_deinit(&ctx);
    assert(ctx.initialized == 0u);
    assert(ctx.rx_pos == 0u);
    assert(ctx.saved_len == 0u);
    assert(ctx.hist_offset == 0);
    assert(ctx.escape_state == 0u);
    assert(ctx.crlf_skip_lf == 0u);
    printf("PASSED\n");
}

static void test_deinit_twice(void)
{
    printf("Test: a second deinit is harmless... ");
    reset();
    elib_cli_deinit(&ctx);
    elib_cli_deinit(&ctx);
    assert(ctx.initialized == 0u);
    printf("PASSED\n");
}

static void test_apis_reject_after_deinit(void)
{
    printf("Test: every API rejects a deinitialized context... ");
    reset();
    elib_cli_deinit(&ctx);

    assert(elib_cli_feed_char(&ctx, 'a') == ELIB_CLI_ERR_NOT_INITIALIZED);
    assert(elib_cli_register(&ctx, "x", NULL, on_cmd)
           == ELIB_CLI_ERR_NOT_INITIALIZED);
    assert(calls == 0u);
    printf("PASSED\n");
}

static void test_null_ctx_is_rejected(void)
{
    printf("Test: a NULL context is rejected by every API... ");
    uint32_t len = 0u;

    assert(elib_cli_feed_char(NULL, 'a') == ELIB_CLI_ERR_INVALID_PARAM);
    assert(elib_cli_register(NULL, "x", NULL, on_cmd)
           == ELIB_CLI_ERR_INVALID_PARAM);
    assert(elib_cli_init(NULL, NULL) == ELIB_CLI_ERR_INVALID_PARAM);
    elib_cli_deinit(NULL);
    /* the pure string helpers do not need a context at all */
    assert(elib_cli_argc(NULL) == 0u);
    assert(elib_cli_argv(NULL, 0u, &len) == NULL);
    printf("PASSED\n");
}

static void test_zeroed_ctx_is_rejected(void)
{
    printf("Test: a zeroed context is not initialized... ");
    memset(&ctx, 0, sizeof(ctx));
    assert(elib_cli_feed_char(&ctx, 'a') == ELIB_CLI_ERR_NOT_INITIALIZED);
    printf("PASSED\n");
}

static void test_deinit_keeps_the_configuration(void)
{
    printf("Test: deinit leaves the copied configuration readable... ");
    reset();
    elib_cli_deinit(&ctx);
    assert(ctx.cfg.prompt != NULL);
    assert(strcmp(ctx.cfg.prompt, "> ") == 0);
    assert(ctx.cfg.rx_buf == rx);
    assert(ctx.cfg.rx_buf_size == RX_SIZE);
    assert(ctx.cfg.history_buf == hist);
    assert(ctx.cfg.cmd_table == table);
    assert(ctx.cfg.print == capture);
    printf("PASSED\n");
}

static void test_reinit_revives(void)
{
    printf("Test: init revives a deinitialized context... ");
    reset();
    elib_cli_deinit(&ctx);
    assert(elib_cli_feed_char(&ctx, 'a') == ELIB_CLI_ERR_NOT_INITIALIZED);

    {
        elib_cli_cfg_t cfg = make_cfg(rx, RX_SIZE, hist, table, TABLE_SIZE,
                                      "> ");

        assert(elib_cli_init(&ctx, &cfg) == ELIB_CLI_OK);
    }
    assert(elib_cli_register(&ctx, "led", NULL, on_cmd) == ELIB_CLI_OK);
    feed("led\n");
    assert(calls == 1u);
    assert(strcmp(out, "led\n> ") == 0);
    printf("PASSED\n");
}

static void test_reinit_drops_the_session(void)
{
    printf("Test: re-init starts from a clean session... ");
    reset();
    feed("led\n");
    assert(strcmp(hist, "led\n") == 0);
    assert(ctx.cmd_count == 1u);

    {
        elib_cli_cfg_t cfg = make_cfg(rx, RX_SIZE, hist, table, TABLE_SIZE,
                                      "> ");

        assert(elib_cli_init(&ctx, &cfg) == ELIB_CLI_OK);
    }
    assert(ctx.cmd_count == 0u);
    assert(ctx.rx_pos == 0u);
    assert(strcmp(hist, "") == 0);
    assert(strcmp(rx, "") == 0);
    assert(calls == 1u);                   /* the old call is not repeated */
    printf("PASSED\n");
}

static void test_two_contexts_are_independent(void)
{
    printf("Test: two contexts share no state... ");
    static char rx_a[RX_SIZE];
    static char rx_b[RX_SIZE];
    static char hist_a[HIST_SIZE];
    static char hist_b[HIST_SIZE];
    static elib_cli_cmd_t table_a[2];
    static elib_cli_cmd_t table_b[2];
    static elib_cli_ctx_t ctx_a;
    static elib_cli_ctx_t ctx_b;
    elib_cli_cfg_t cfg;

    memset(rx_a, 0, sizeof(rx_a));
    memset(rx_b, 0, sizeof(rx_b));
    memset(hist_a, 0, sizeof(hist_a));
    memset(hist_b, 0, sizeof(hist_b));
    memset(table_a, 0, sizeof(table_a));
    memset(table_b, 0, sizeof(table_b));
    memset(&ctx_a, 0, sizeof(ctx_a));
    memset(&ctx_b, 0, sizeof(ctx_b));
    out_len = 0u;
    out[0] = '\0';
    calls = 0u;

    cfg = make_cfg(rx_a, RX_SIZE, hist_a, table_a, 2u, "A> ");
    assert(elib_cli_init(&ctx_a, &cfg) == ELIB_CLI_OK);
    cfg = make_cfg(rx_b, RX_SIZE, hist_b, table_b, 2u, "B> ");
    assert(elib_cli_init(&ctx_b, &cfg) == ELIB_CLI_OK);

    assert(elib_cli_register(&ctx_a, "only_a", NULL, on_cmd) == ELIB_CLI_OK);
    assert(ctx_a.cmd_count == 1u);
    assert(ctx_b.cmd_count == 0u);

    assert(elib_cli_feed_char(&ctx_a, 'x') == ELIB_CLI_OK);
    assert(ctx_a.rx_pos == 1u);
    assert(ctx_b.rx_pos == 0u);
    assert(strcmp(rx_a, "x") == 0);
    assert(strcmp(rx_b, "") == 0);

    /* deinit one, the other keeps working */
    elib_cli_deinit(&ctx_a);
    assert(elib_cli_feed_char(&ctx_a, 'y') == ELIB_CLI_ERR_NOT_INITIALIZED);
    assert(elib_cli_feed_char(&ctx_b, 'z') == ELIB_CLI_OK);
    assert(strcmp(rx_b, "z") == 0);

    /* the prompt follows each context */
    out_len = 0u;
    out[0] = '\0';
    assert(elib_cli_feed_char(&ctx_b, '\n') == ELIB_CLI_ERR_CMD_NOT_FOUND);
    assert(strcmp(out, "unknown command: z\nB> ") == 0);
    printf("PASSED\n");
}

static void test_init_after_deinit_of_dirty_state(void)
{
    printf("Test: init after deinit re-arms the decoder... ");
    reset();
    feed("\x1b");
    assert(ctx.escape_state != 0u);
    elib_cli_deinit(&ctx);

    {
        elib_cli_cfg_t cfg = make_cfg(rx, RX_SIZE, hist, table, TABLE_SIZE,
                                      "> ");

        assert(elib_cli_init(&ctx, &cfg) == ELIB_CLI_OK);
    }
    assert(ctx.escape_state == 0u);
    assert(elib_cli_register(&ctx, "led", NULL, on_cmd) == ELIB_CLI_OK);
    feed("led\n");
    assert(calls == 1u);
    printf("PASSED\n");
}

int main(void)
{
    printf("=== test_lifecycle ===\n");
    test_deinit_is_null_safe();
    test_deinit_clears_the_state();
    test_deinit_twice();
    test_apis_reject_after_deinit();
    test_null_ctx_is_rejected();
    test_zeroed_ctx_is_rejected();
    test_deinit_keeps_the_configuration();
    test_reinit_revives();
    test_reinit_drops_the_session();
    test_two_contexts_are_independent();
    test_init_after_deinit_of_dirty_state();
    printf("=== test_lifecycle: all PASSED ===\n");
    return 0;
}
