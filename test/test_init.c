/* test_init.c --- init validation, cfg copy by value, re-initialization, buffer reset */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include "elib_cli.h"

#define RX_SIZE     32u
#define HIST_SIZE   64u
#define SAVED_SIZE  16u
#define TABLE_SIZE  8u
#define OUT_SIZE    128u

static elib_cli_ctx_t ctx;
static char rx[RX_SIZE];
static char hist[HIST_SIZE];
static char saved[SAVED_SIZE];
static elib_cli_cmd_t table[TABLE_SIZE];
static char out[OUT_SIZE];
static size_t out_len;

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

static void on_cmd(void *ud, const char *args) { (void)ud; (void)args; }

static elib_cli_cfg_t good_cfg(void)
{
    elib_cli_cfg_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.prompt = "> ";
    cfg.newline = ELIB_CLI_NL_LF;
    cfg.print = capture;
    cfg.user_data = NULL;
    cfg.rx_buf = rx;
    cfg.rx_buf_size = RX_SIZE;
    cfg.history_buf = hist;
    cfg.history_buf_size = HIST_SIZE;
    cfg.cmd_table = table;
    cfg.cmd_table_size = TABLE_SIZE;
    cfg.saved_input = saved;
    cfg.saved_buf_size = SAVED_SIZE;
    cfg.flags = ELIB_CLI_FLAG_ECHO;
    return cfg;
}

static void clear_buffers(void)
{
    memset(rx, 0, sizeof(rx));
    memset(hist, 0, sizeof(hist));
    memset(saved, 0, sizeof(saved));
    memset(table, 0, sizeof(table));
    memset(&ctx, 0, sizeof(ctx));
    out_len = 0u;
    out[0] = '\0';
}

static void test_version(void)
{
    printf("Test: version macros... ");
    assert(strcmp(ELIB_CLI_VERSION, "2.0.0") == 0);
    assert(ELIB_CLI_VERSION_MAJOR == 2);
    assert(ELIB_CLI_VERSION_MINOR == 0);
    assert(ELIB_CLI_VERSION_PATCH == 0);
    printf("PASSED\n");
}

static void test_limits(void)
{
    printf("Test: buffer minimums and flag bits... ");
    assert(ELIB_CLI_MIN_RX_BUF_SIZE == 2u);
    assert(ELIB_CLI_MIN_HIST_BUF_SIZE == 3u);
    assert(ELIB_CLI_MIN_SAVED_BUF_SIZE == 2u);
    assert(ELIB_CLI_FLAG_ECHO == 1u);
    assert(ELIB_CLI_OK == 0);
    assert(ELIB_CLI_ERR_INVALID_PARAM != ELIB_CLI_OK);
    assert(ELIB_CLI_ERR_NOT_INITIALIZED != ELIB_CLI_ERR_INVALID_PARAM);
    assert(ELIB_CLI_ERR_CMD_TABLE_FULL != ELIB_CLI_ERR_NOT_INITIALIZED);
    assert(ELIB_CLI_ERR_RX_OVERFLOW != ELIB_CLI_ERR_CMD_TABLE_FULL);
    assert(ELIB_CLI_ERR_CMD_NOT_FOUND != ELIB_CLI_ERR_RX_OVERFLOW);
    printf("PASSED\n");
}

static void test_init_copies_cfg(void)
{
    printf("Test: init copies every cfg field into the context... ");
    elib_cli_cfg_t cfg = good_cfg();
    int marker = 0;

    cfg.user_data = &marker;
    cfg.flags = 0u;
    cfg.newline = ELIB_CLI_NL_CRLF;
    clear_buffers();
    assert(elib_cli_init(&ctx, &cfg) == ELIB_CLI_OK);

    assert(ctx.cfg.prompt == cfg.prompt);
    assert(ctx.cfg.newline == ELIB_CLI_NL_CRLF);
    assert(ctx.cfg.print == capture);
    assert(ctx.cfg.user_data == &marker);
    assert(ctx.cfg.rx_buf == rx);
    assert(ctx.cfg.rx_buf_size == RX_SIZE);
    assert(ctx.cfg.history_buf == hist);
    assert(ctx.cfg.history_buf_size == HIST_SIZE);
    assert(ctx.cfg.cmd_table == table);
    assert(ctx.cfg.cmd_table_size == TABLE_SIZE);
    assert(ctx.cfg.saved_input == saved);
    assert(ctx.cfg.saved_buf_size == SAVED_SIZE);
    assert(ctx.cfg.flags == 0u);
    assert(ctx.initialized == 1u);
    assert(ctx.cmd_count == 0u);
    assert(ctx.rx_pos == 0u);
    assert(ctx.saved_len == 0u);
    assert(ctx.hist_offset == 0);
    printf("PASSED\n");
}

static void test_init_cfg_is_a_value(void)
{
    printf("Test: the cfg argument may live on the stack... ");
    clear_buffers();
    {
        elib_cli_cfg_t cfg = good_cfg();

        assert(elib_cli_init(&ctx, &cfg) == ELIB_CLI_OK);
        memset(&cfg, 0xAA, sizeof(cfg));       /* destroy the original */
    }
    assert(ctx.cfg.prompt != NULL);
    assert(strcmp(ctx.cfg.prompt, "> ") == 0);
    assert(ctx.cfg.print == capture);
    assert(ctx.cfg.rx_buf_size == RX_SIZE);
    printf("PASSED\n");
}

static void test_init_rejects_null(void)
{
    printf("Test: init rejects NULL ctx and NULL cfg... ");
    elib_cli_cfg_t cfg = good_cfg();

    clear_buffers();
    assert(elib_cli_init(NULL, &cfg) == ELIB_CLI_ERR_INVALID_PARAM);
    assert(elib_cli_init(&ctx, NULL) == ELIB_CLI_ERR_INVALID_PARAM);
    printf("PASSED\n");
}

static void test_init_requires_print(void)
{
    printf("Test: init requires a print callback... ");
    elib_cli_cfg_t cfg = good_cfg();

    clear_buffers();
    cfg.print = NULL;
    assert(elib_cli_init(&ctx, &cfg) == ELIB_CLI_ERR_INVALID_PARAM);
    printf("PASSED\n");
}

static void test_init_receive_buffer(void)
{
    printf("Test: init enforces the receive-buffer minimum... ");
    elib_cli_cfg_t cfg = good_cfg();

    clear_buffers();
    cfg.rx_buf = NULL;
    assert(elib_cli_init(&ctx, &cfg) == ELIB_CLI_ERR_INVALID_PARAM);

    cfg = good_cfg();
    cfg.rx_buf_size = 0u;
    assert(elib_cli_init(&ctx, &cfg) == ELIB_CLI_ERR_INVALID_PARAM);

    cfg.rx_buf_size = ELIB_CLI_MIN_RX_BUF_SIZE - 1u;
    assert(elib_cli_init(&ctx, &cfg) == ELIB_CLI_ERR_INVALID_PARAM);

    cfg.rx_buf_size = ELIB_CLI_MIN_RX_BUF_SIZE;
    assert(elib_cli_init(&ctx, &cfg) == ELIB_CLI_OK);
    assert(ctx.cfg.rx_buf_size == ELIB_CLI_MIN_RX_BUF_SIZE);
    printf("PASSED\n");
}

static void test_init_requires_cmd_table(void)
{
    printf("Test: init requires a command table... ");
    elib_cli_cfg_t cfg = good_cfg();

    clear_buffers();
    cfg.cmd_table = NULL;
    assert(elib_cli_init(&ctx, &cfg) == ELIB_CLI_ERR_INVALID_PARAM);

    cfg = good_cfg();
    cfg.cmd_table_size = 0u;
    assert(elib_cli_init(&ctx, &cfg) == ELIB_CLI_ERR_INVALID_PARAM);
    printf("PASSED\n");
}

static void test_init_history_buffer(void)
{
    printf("Test: the history buffer is either absent or at least 3 bytes... ");
    elib_cli_cfg_t cfg = good_cfg();

    clear_buffers();
    cfg.history_buf = NULL;
    cfg.history_buf_size = 0u;
    assert(elib_cli_init(&ctx, &cfg) == ELIB_CLI_OK);

    cfg = good_cfg();
    cfg.history_buf = NULL;                 /* half specified */
    assert(elib_cli_init(&ctx, &cfg) == ELIB_CLI_ERR_INVALID_PARAM);

    cfg = good_cfg();
    cfg.history_buf_size = 0u;              /* half specified */
    assert(elib_cli_init(&ctx, &cfg) == ELIB_CLI_ERR_INVALID_PARAM);

    cfg = good_cfg();
    cfg.history_buf_size = ELIB_CLI_MIN_HIST_BUF_SIZE - 1u;
    assert(elib_cli_init(&ctx, &cfg) == ELIB_CLI_ERR_INVALID_PARAM);

    cfg.history_buf_size = ELIB_CLI_MIN_HIST_BUF_SIZE;
    assert(elib_cli_init(&ctx, &cfg) == ELIB_CLI_OK);
    printf("PASSED\n");
}

static void test_init_saved_buffer(void)
{
    printf("Test: the saved-input buffer is either absent or at least 2 bytes... ");
    elib_cli_cfg_t cfg = good_cfg();

    clear_buffers();
    cfg.saved_input = NULL;
    cfg.saved_buf_size = 0u;
    assert(elib_cli_init(&ctx, &cfg) == ELIB_CLI_OK);

    cfg = good_cfg();
    cfg.saved_input = NULL;
    assert(elib_cli_init(&ctx, &cfg) == ELIB_CLI_ERR_INVALID_PARAM);

    cfg = good_cfg();
    cfg.saved_buf_size = 0u;
    assert(elib_cli_init(&ctx, &cfg) == ELIB_CLI_ERR_INVALID_PARAM);

    cfg = good_cfg();
    cfg.saved_buf_size = ELIB_CLI_MIN_SAVED_BUF_SIZE - 1u;
    assert(elib_cli_init(&ctx, &cfg) == ELIB_CLI_ERR_INVALID_PARAM);

    cfg.saved_buf_size = ELIB_CLI_MIN_SAVED_BUF_SIZE;
    assert(elib_cli_init(&ctx, &cfg) == ELIB_CLI_OK);
    printf("PASSED\n");
}

static void test_init_newline_range(void)
{
    printf("Test: init rejects an out-of-range line terminator... ");
    elib_cli_cfg_t cfg = good_cfg();

    clear_buffers();
    cfg.newline = ELIB_CLI_NL_LF;
    assert(elib_cli_init(&ctx, &cfg) == ELIB_CLI_OK);
    cfg.newline = ELIB_CLI_NL_CR;
    assert(elib_cli_init(&ctx, &cfg) == ELIB_CLI_OK);
    cfg.newline = ELIB_CLI_NL_CRLF;
    assert(elib_cli_init(&ctx, &cfg) == ELIB_CLI_OK);

    cfg.newline = (elib_cli_nl_t)3;
    assert(elib_cli_init(&ctx, &cfg) == ELIB_CLI_ERR_INVALID_PARAM);
    cfg.newline = (elib_cli_nl_t)(-1);
    assert(elib_cli_init(&ctx, &cfg) == ELIB_CLI_ERR_INVALID_PARAM);
    printf("PASSED\n");
}

static void test_init_rejects_unknown_flags(void)
{
    printf("Test: init rejects flag bits it does not understand... ");
    elib_cli_cfg_t cfg = good_cfg();

    clear_buffers();
    cfg.flags = 1u << 1;
    assert(elib_cli_init(&ctx, &cfg) == ELIB_CLI_ERR_INVALID_PARAM);

    cfg.flags = ELIB_CLI_FLAG_ECHO | (1u << 7);
    assert(elib_cli_init(&ctx, &cfg) == ELIB_CLI_ERR_INVALID_PARAM);

    cfg.flags = ELIB_CLI_FLAG_ECHO;
    assert(elib_cli_init(&ctx, &cfg) == ELIB_CLI_OK);
    printf("PASSED\n");
}

static void test_rejected_init_leaves_ctx_untouched(void)
{
    printf("Test: a rejected init does not modify the context... ");
    elib_cli_cfg_t cfg = good_cfg();
    elib_cli_ctx_t snapshot;

    memset(&ctx, 0x5A, sizeof(ctx));
    memcpy(&snapshot, &ctx, sizeof(ctx));
    cfg.print = NULL;
    assert(elib_cli_init(&ctx, &cfg) == ELIB_CLI_ERR_INVALID_PARAM);
    assert(memcmp(&ctx, &snapshot, sizeof(ctx)) == 0);
    printf("PASSED\n");
}

static void test_init_reinitializes_dirty_ctx(void)
{
    printf("Test: init overwrites a dirty context unconditionally... ");
    elib_cli_cfg_t cfg = good_cfg();

    memset(&ctx, 0xAA, sizeof(ctx));
    assert(elib_cli_init(&ctx, &cfg) == ELIB_CLI_OK);
    assert(ctx.initialized == 1u);
    assert(ctx.cmd_count == 0u);
    assert(ctx.rx_pos == 0u);
    assert(ctx.saved_len == 0u);
    assert(ctx.hist_offset == 0);
    assert(ctx.escape_state == 0u);
    assert(ctx.crlf_skip_lf == 0u);
    assert(ctx.cfg.rx_buf_size == RX_SIZE);
    printf("PASSED\n");
}

static void test_init_resets_the_user_buffers(void)
{
    printf("Test: init empties the caller buffers... ");
    elib_cli_cfg_t cfg = good_cfg();

    memset(rx, 'x', sizeof(rx));
    memset(hist, 'y', sizeof(hist));
    memset(saved, 'z', sizeof(saved));
    memset(&ctx, 0, sizeof(ctx));
    out_len = 0u;
    assert(elib_cli_init(&ctx, &cfg) == ELIB_CLI_OK);

    assert(rx[0] == '\0');
    assert(hist[0] == '\0');
    assert(saved[0] == '\0');
    printf("PASSED\n");
}

static void test_init_prints_nothing(void)
{
    printf("Test: init only prepares resources, it prints nothing... ");
    elib_cli_cfg_t cfg = good_cfg();

    clear_buffers();
    assert(elib_cli_init(&ctx, &cfg) == ELIB_CLI_OK);
    assert(out_len == 0u);
    printf("PASSED\n");
}

static void test_init_accepts_null_prompt(void)
{
    printf("Test: a NULL prompt is legal... ");
    elib_cli_cfg_t cfg = good_cfg();

    clear_buffers();
    cfg.prompt = NULL;
    cfg.flags = 0u;
    assert(elib_cli_init(&ctx, &cfg) == ELIB_CLI_OK);
    assert(ctx.cfg.prompt == NULL);

    assert(elib_cli_register(&ctx, "a", NULL, on_cmd) == ELIB_CLI_OK);
    assert(elib_cli_feed_char(&ctx, 'a') == ELIB_CLI_OK);
    assert(elib_cli_feed_char(&ctx, '\n') == ELIB_CLI_OK);
    assert(out_len == 1u);                 /* just the terminator */
    assert(strcmp(out, "\n") == 0);
    printf("PASSED\n");
}

int main(void)
{
    printf("=== test_init ===\n");
    test_version();
    test_limits();
    test_init_copies_cfg();
    test_init_cfg_is_a_value();
    test_init_rejects_null();
    test_init_requires_print();
    test_init_receive_buffer();
    test_init_requires_cmd_table();
    test_init_history_buffer();
    test_init_saved_buffer();
    test_init_newline_range();
    test_init_rejects_unknown_flags();
    test_rejected_init_leaves_ctx_untouched();
    test_init_reinitializes_dirty_ctx();
    test_init_resets_the_user_buffers();
    test_init_prints_nothing();
    test_init_accepts_null_prompt();
    printf("=== test_init: all PASSED ===\n");
    return 0;
}
