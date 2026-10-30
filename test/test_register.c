/* test_register.c --- registration, replacement, table limits, state gating */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include "elib_cli.h"

#define RX_SIZE     32u
#define HIST_SIZE   64u
#define SAVED_SIZE  16u
#define TABLE_SIZE  3u
#define OUT_SIZE    128u

static elib_cli_ctx_t ctx;
static char rx[RX_SIZE];
static char hist[HIST_SIZE];
static char saved[SAVED_SIZE];
static elib_cli_cmd_t table[TABLE_SIZE];
static char out[OUT_SIZE];
static size_t out_len;
static unsigned calls_a;
static unsigned calls_b;
static unsigned calls_c;
static unsigned calls_d;

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

static void cb_a(void *ud, const char *args) { (void)args; assert(ud == NULL); calls_a++; }
static void cb_b(void *ud, const char *args) { (void)args; assert(ud == NULL); calls_b++; }
static void cb_c(void *ud, const char *args) { (void)args; assert(ud == NULL); calls_c++; }
static void cb_d(void *ud, const char *args) { (void)args; assert(ud == NULL); calls_d++; }

static void reset(void)
{
    elib_cli_cfg_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    memset(rx, 0, sizeof(rx));
    memset(hist, 0, sizeof(hist));
    memset(saved, 0, sizeof(saved));
    memset(table, 0, sizeof(table));
    memset(&ctx, 0, sizeof(ctx));
    out_len = 0u;
    out[0] = '\0';
    calls_a = 0u;
    calls_b = 0u;
    calls_c = 0u;
    calls_d = 0u;

    cfg.prompt = "> ";
    cfg.newline = ELIB_CLI_NL_LF;
    cfg.print = capture;
    cfg.rx_buf = rx;
    cfg.rx_buf_size = RX_SIZE;
    cfg.history_buf = hist;
    cfg.history_buf_size = HIST_SIZE;
    cfg.cmd_table = table;
    cfg.cmd_table_size = TABLE_SIZE;
    cfg.saved_input = saved;
    cfg.saved_buf_size = SAVED_SIZE;
    cfg.flags = ELIB_CLI_FLAG_ECHO;
    assert(elib_cli_init(&ctx, &cfg) == ELIB_CLI_OK);
}

static void feed(const char *s)
{
    while (*s != '\0') {
        (void)elib_cli_feed_char(&ctx, *s);
        s++;
    }
}

static void test_register_rejects_bad_params(void)
{
    printf("Test: register rejects NULL and empty arguments... ");
    static char name[] = "led";

    reset();
    assert(elib_cli_register(NULL, name, NULL, cb_a)
           == ELIB_CLI_ERR_INVALID_PARAM);
    assert(elib_cli_register(&ctx, NULL, NULL, cb_a)
           == ELIB_CLI_ERR_INVALID_PARAM);
    assert(elib_cli_register(&ctx, "", NULL, cb_a)
           == ELIB_CLI_ERR_INVALID_PARAM);
    assert(elib_cli_register(&ctx, name, NULL, NULL)
           == ELIB_CLI_ERR_INVALID_PARAM);
    assert(ctx.cmd_count == 0u);
    printf("PASSED\n");
}

static void test_register_requires_init(void)
{
    printf("Test: register rejects a context that was never initialized... ");
    memset(&ctx, 0, sizeof(ctx));
    assert(elib_cli_register(&ctx, "led", NULL, cb_a)
           == ELIB_CLI_ERR_NOT_INITIALIZED);
    printf("PASSED\n");
}

static void test_register_requires_live_ctx(void)
{
    printf("Test: register rejects a deinitialized context... ");
    reset();
    elib_cli_deinit(&ctx);
    assert(elib_cli_register(&ctx, "led", NULL, cb_a)
           == ELIB_CLI_ERR_NOT_INITIALIZED);
    printf("PASSED\n");
}

static void test_register_appends(void)
{
    printf("Test: register appends to the table in order... ");
    static char name_a[] = "aaa";
    static char name_b[] = "bbb";

    reset();
    assert(elib_cli_register(&ctx, name_a, "A help", cb_a) == ELIB_CLI_OK);
    assert(elib_cli_register(&ctx, name_b, NULL, cb_b) == ELIB_CLI_OK);

    assert(ctx.cmd_count == 2u);
    assert(ctx.cfg.cmd_table[0].name == name_a);   /* stored, not copied */
    assert(strcmp(ctx.cfg.cmd_table[0].help, "A help") == 0);
    assert(ctx.cfg.cmd_table[0].callback == cb_a);
    assert(ctx.cfg.cmd_table[1].name == name_b);
    assert(ctx.cfg.cmd_table[1].help == NULL);
    assert(ctx.cfg.cmd_table[1].callback == cb_b);
    printf("PASSED\n");
}

static void test_register_dispatches(void)
{
    printf("Test: a registered command is dispatched by name... ");
    reset();
    assert(elib_cli_register(&ctx, "aaa", NULL, cb_a) == ELIB_CLI_OK);
    assert(elib_cli_register(&ctx, "bbb", NULL, cb_b) == ELIB_CLI_OK);

    feed("aaa\n");
    assert(calls_a == 1u);
    assert(calls_b == 0u);

    feed("bbb\n");
    assert(calls_a == 1u);
    assert(calls_b == 1u);
    printf("PASSED\n");
}

static void test_register_replaces(void)
{
    printf("Test: re-registering a name keeps the slot and swaps the handler... ");
    reset();
    assert(elib_cli_register(&ctx, "led", "old", cb_a) == ELIB_CLI_OK);
    assert(elib_cli_register(&ctx, "led", "new", cb_b) == ELIB_CLI_OK);

    assert(ctx.cmd_count == 1u);
    assert(strcmp(ctx.cfg.cmd_table[0].help, "new") == 0);
    assert(ctx.cfg.cmd_table[0].callback == cb_b);

    feed("led\n");
    assert(calls_a == 0u);
    assert(calls_b == 1u);
    printf("PASSED\n");
}

static void test_register_is_case_sensitive(void)
{
    printf("Test: names are case sensitive... ");
    reset();
    assert(elib_cli_register(&ctx, "led", NULL, cb_a) == ELIB_CLI_OK);
    assert(elib_cli_register(&ctx, "LED", NULL, cb_b) == ELIB_CLI_OK);
    assert(ctx.cmd_count == 2u);

    feed("LED\n");
    assert(calls_b == 1u);
    assert(calls_a == 0u);
    printf("PASSED\n");
}

static void test_register_table_full(void)
{
    printf("Test: a full table reports CMD_TABLE_FULL... ");
    reset();
    assert(elib_cli_register(&ctx, "a", NULL, cb_a) == ELIB_CLI_OK);
    assert(elib_cli_register(&ctx, "b", NULL, cb_b) == ELIB_CLI_OK);
    assert(elib_cli_register(&ctx, "c", NULL, cb_c) == ELIB_CLI_OK);
    assert(ctx.cmd_count == TABLE_SIZE);
    assert(elib_cli_register(&ctx, "d", NULL, cb_d)
           == ELIB_CLI_ERR_CMD_TABLE_FULL);
    assert(ctx.cmd_count == TABLE_SIZE);
    printf("PASSED\n");
}

static void test_register_replace_works_when_full(void)
{
    printf("Test: a full table still accepts a replacement... ");
    reset();
    assert(elib_cli_register(&ctx, "a", NULL, cb_a) == ELIB_CLI_OK);
    assert(elib_cli_register(&ctx, "b", NULL, cb_b) == ELIB_CLI_OK);
    assert(elib_cli_register(&ctx, "c", NULL, cb_c) == ELIB_CLI_OK);
    assert(elib_cli_register(&ctx, "b", "b2", cb_d) == ELIB_CLI_OK);
    assert(ctx.cmd_count == TABLE_SIZE);
    assert(ctx.cfg.cmd_table[1].callback == cb_d);

    feed("b\n");
    assert(calls_d == 1u);
    assert(calls_b == 0u);
    printf("PASSED\n");
}

static void test_register_help_is_a_normal_command(void)
{
    printf("Test: registering help occupies a normal slot... ");
    reset();
    assert(elib_cli_register(&ctx, "help", "mine", cb_a) == ELIB_CLI_OK);
    assert(ctx.cmd_count == 1u);

    feed("help\n");
    assert(calls_a == 1u);                 /* the user handler, not the listing */
    printf("PASSED\n");
}

static void test_register_after_deinit_reinit(void)
{
    printf("Test: re-init after deinit clears the table... ");
    reset();
    assert(elib_cli_register(&ctx, "a", NULL, cb_a) == ELIB_CLI_OK);
    assert(ctx.cmd_count == 1u);

    elib_cli_deinit(&ctx);
    {
        elib_cli_cfg_t cfg;

        memset(&cfg, 0, sizeof(cfg));
        memset(rx, 0, sizeof(rx));
        memset(hist, 0, sizeof(hist));
        memset(saved, 0, sizeof(saved));
        memset(table, 0, sizeof(table));
        cfg.prompt = "> ";
        cfg.newline = ELIB_CLI_NL_LF;
        cfg.print = capture;
        cfg.rx_buf = rx;
        cfg.rx_buf_size = RX_SIZE;
        cfg.history_buf = hist;
        cfg.history_buf_size = HIST_SIZE;
        cfg.cmd_table = table;
        cfg.cmd_table_size = TABLE_SIZE;
        cfg.saved_input = saved;
        cfg.saved_buf_size = SAVED_SIZE;
        cfg.flags = ELIB_CLI_FLAG_ECHO;
        assert(elib_cli_init(&ctx, &cfg) == ELIB_CLI_OK);
    }
    assert(ctx.cmd_count == 0u);
    assert(elib_cli_register(&ctx, "b", NULL, cb_b) == ELIB_CLI_OK);
    feed("b\n");
    assert(calls_b == 1u);
    printf("PASSED\n");
}

int main(void)
{
    printf("=== test_register ===\n");
    test_register_rejects_bad_params();
    test_register_requires_init();
    test_register_requires_live_ctx();
    test_register_appends();
    test_register_dispatches();
    test_register_replaces();
    test_register_is_case_sensitive();
    test_register_table_full();
    test_register_replace_works_when_full();
    test_register_help_is_a_normal_command();
    test_register_after_deinit_reinit();
    printf("=== test_register: all PASSED ===\n");
    return 0;
}
