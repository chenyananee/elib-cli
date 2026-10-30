/* test_line_edit.c --- printable input, backspace, overflow, ignored bytes */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include "elib_cli.h"

#define RX_SIZE     8u
#define HIST_SIZE   64u
#define TABLE_SIZE  2u
#define OUT_SIZE    256u

static elib_cli_ctx_t ctx;
static char rx[RX_SIZE];
static char hist[HIST_SIZE];
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

static void reset(void)
{
    elib_cli_cfg_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    memset(rx, 0, sizeof(rx));
    memset(hist, 0, sizeof(hist));
    memset(table, 0, sizeof(table));
    memset(&ctx, 0, sizeof(ctx));
    out_len = 0u;
    out[0] = '\0';

    cfg.prompt = "> ";
    cfg.newline = ELIB_CLI_NL_LF;
    cfg.print = capture;
    cfg.rx_buf = rx;
    cfg.rx_buf_size = RX_SIZE;
    cfg.history_buf = hist;
    cfg.history_buf_size = HIST_SIZE;
    cfg.cmd_table = table;
    cfg.cmd_table_size = TABLE_SIZE;
    cfg.flags = ELIB_CLI_FLAG_ECHO;
    assert(elib_cli_init(&ctx, &cfg) == ELIB_CLI_OK);
}

static elib_cli_err_t feed(const char *s)
{
    elib_cli_err_t err = ELIB_CLI_OK;

    while (*s != '\0') {
        err = elib_cli_feed_char(&ctx, *s);
        s++;
    }
    return err;
}

static void test_null_ctx(void)
{
    printf("Test: feed_char rejects a NULL context... ");
    assert(elib_cli_feed_char(NULL, 'a') == ELIB_CLI_ERR_INVALID_PARAM);
    printf("PASSED\n");
}

static void test_typing_echoes(void)
{
    printf("Test: every printable byte is appended and echoed... ");
    reset();
    assert(feed("abc") == ELIB_CLI_OK);
    assert(ctx.rx_pos == 3u);
    assert(strcmp(rx, "abc") == 0);
    assert(strcmp(out, "abc") == 0);
    printf("PASSED\n");
}

static void test_full_printable_range(void)
{
    printf("Test: 0x20 and 0x7E are printable... ");
    reset();
    assert(elib_cli_feed_char(&ctx, 0x20) == ELIB_CLI_OK);
    assert(elib_cli_feed_char(&ctx, 0x7E) == ELIB_CLI_OK);
    assert(ctx.rx_pos == 2u);
    assert(rx[0] == 0x20);
    assert(rx[1] == 0x7E);
    assert(rx[2] == '\0');
    printf("PASSED\n");
}

static void test_backspace(void)
{
    printf("Test: backspace removes one byte and echoes the erase... ");
    reset();
    assert(feed("abc") == ELIB_CLI_OK);
    assert(elib_cli_feed_char(&ctx, 0x08) == ELIB_CLI_OK);
    assert(ctx.rx_pos == 2u);
    assert(strcmp(rx, "ab") == 0);
    assert(strcmp(out, "abc\b \b") == 0);

    assert(elib_cli_feed_char(&ctx, 0x7F) == ELIB_CLI_OK);   /* DEL */
    assert(ctx.rx_pos == 1u);
    assert(strcmp(rx, "a") == 0);
    assert(strcmp(out, "abc\b \b\b \b") == 0);
    printf("PASSED\n");
}

static void test_backspace_at_start_is_silent(void)
{
    printf("Test: backspace on an empty line prints nothing... ");
    reset();
    assert(elib_cli_feed_char(&ctx, 0x08) == ELIB_CLI_OK);
    assert(elib_cli_feed_char(&ctx, 0x7F) == ELIB_CLI_OK);
    assert(ctx.rx_pos == 0u);
    assert(rx[0] == '\0');
    assert(out_len == 0u);
    printf("PASSED\n");
}

static void test_control_bytes_are_ignored(void)
{
    printf("Test: control bytes never reach the line... ");
    static const unsigned char junk[] = { 0x00u, 0x01u, 0x02u, 0x03u, 0x04u,
                                          0x05u, 0x06u, 0x07u, 0x0Bu, 0x0Cu,
                                          0x0Eu, 0x0Fu, 0x10u, 0x1Fu, 0x80u,
                                          0xFFu };
    size_t i;

    reset();
    assert(feed("ab") == ELIB_CLI_OK);
    for (i = 0u; i < sizeof(junk); i++) {
        assert(elib_cli_feed_char(&ctx, (char)junk[i]) == ELIB_CLI_OK);
    }
    assert(ctx.rx_pos == 2u);
    assert(strcmp(rx, "ab") == 0);
    assert(strcmp(out, "ab") == 0);
    printf("PASSED\n");
}

static void test_overflow(void)
{
    printf("Test: a full line reports RX_OVERFLOW and keeps the old bytes... ");
    reset();                                /* RX_SIZE 8 -> 7 usable bytes */
    assert(feed("1234567") == ELIB_CLI_OK);
    assert(ctx.rx_pos == 7u);

    assert(elib_cli_feed_char(&ctx, '8') == ELIB_CLI_ERR_RX_OVERFLOW);
    assert(ctx.rx_pos == 7u);
    assert(strcmp(rx, "1234567") == 0);
    assert(strcmp(out, "1234567") == 0);    /* the dropped byte is not echoed */

    assert(elib_cli_feed_char(&ctx, 0x08) == ELIB_CLI_OK);
    assert(elib_cli_feed_char(&ctx, '8') == ELIB_CLI_OK);
    assert(strcmp(rx, "1234568") == 0);
    printf("PASSED\n");
}

static void test_overflow_repeats(void)
{
    printf("Test: repeated overflow keeps reporting RX_OVERFLOW... ");
    reset();
    assert(feed("1234567") == ELIB_CLI_OK);
    assert(feed("89") == ELIB_CLI_ERR_RX_OVERFLOW);
    assert(strcmp(rx, "1234567") == 0);
    assert(out_len == 7u);
    printf("PASSED\n");
}

static void test_buffer_always_terminated(void)
{
    printf("Test: the receive buffer stays NUL terminated... ");
    reset();
    assert(feed("abc") == ELIB_CLI_OK);
    assert(rx[ctx.rx_pos] == '\0');
    assert(strlen(rx) == ctx.rx_pos);

    assert(elib_cli_feed_char(&ctx, 0x08) == ELIB_CLI_OK);
    assert(rx[ctx.rx_pos] == '\0');
    assert(strlen(rx) == ctx.rx_pos);
    printf("PASSED\n");
}

static void test_del_is_not_stored(void)
{
    printf("Test: 0x7F erases, it is never stored as a byte... ");
    reset();
    assert(feed("ab") == ELIB_CLI_OK);
    assert(elib_cli_feed_char(&ctx, 0x7F) == ELIB_CLI_OK);
    assert(elib_cli_feed_char(&ctx, 0x7F) == ELIB_CLI_OK);
    assert(elib_cli_feed_char(&ctx, 0x7F) == ELIB_CLI_OK);
    assert(ctx.rx_pos == 0u);
    assert(elib_cli_feed_char(&ctx, 'z') == ELIB_CLI_OK);
    assert(strcmp(rx, "z") == 0);
    printf("PASSED\n");
}

static void test_line_survives_non_input_bytes(void)
{
    printf("Test: ignored bytes do not disturb the edited line... ");
    reset();
    assert(feed("led x") == ELIB_CLI_OK);
    assert(elib_cli_feed_char(&ctx, 0x00) == ELIB_CLI_OK);
    assert(elib_cli_feed_char(&ctx, 0x02) == ELIB_CLI_OK);
    assert(strcmp(rx, "led x") == 0);
    assert(ctx.rx_pos == 5u);
    printf("PASSED\n");
}

int main(void)
{
    printf("=== test_line_edit ===\n");
    test_null_ctx();
    test_typing_echoes();
    test_full_printable_range();
    test_backspace();
    test_backspace_at_start_is_silent();
    test_control_bytes_are_ignored();
    test_overflow();
    test_overflow_repeats();
    test_buffer_always_terminated();
    test_del_is_not_stored();
    test_line_survives_non_input_bytes();
    printf("=== test_line_edit: all PASSED ===\n");
    return 0;
}
