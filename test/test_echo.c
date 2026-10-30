/* test_echo.c --- echo on/off mirroring for typing, edits, recall and completion */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include "elib_cli.h"

#define RX_SIZE     32u
#define HIST_SIZE   64u
#define SAVED_SIZE  16u
#define TABLE_SIZE  4u
#define OUT_SIZE    512u

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

static void reset(uint32_t flags)
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
    cfg.flags = flags;
    assert(elib_cli_init(&ctx, &cfg) == ELIB_CLI_OK);
    assert(elib_cli_register(&ctx, "led", NULL, on_cmd) == ELIB_CLI_OK);
    assert(elib_cli_register(&ctx, "link", NULL, on_cmd) == ELIB_CLI_OK);
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

static void test_echo_on_types(void)
{
    printf("Test: echo on mirrors every accepted character... ");
    reset(ELIB_CLI_FLAG_ECHO);
    feed("abc");
    assert(strcmp(out, "abc") == 0);
    printf("PASSED\n");
}

static void test_echo_on_backspace(void)
{
    printf("Test: echo on mirrors an erase as backspace-space-backspace... ");
    reset(ELIB_CLI_FLAG_ECHO);
    feed("ab\x08");
    assert(strcmp(out, "ab\b \b") == 0);
    printf("PASSED\n");
}

static void test_echo_on_submit(void)
{
    printf("Test: echo on still terminates the line and reprompts... ");
    reset(ELIB_CLI_FLAG_ECHO);
    feed("led\n");
    assert(strcmp(out, "led\n> ") == 0);
    printf("PASSED\n");
}

static void test_echo_off_is_silent_while_typing(void)
{
    printf("Test: echo off prints nothing while typing... ");
    reset(0u);
    feed("abc");
    assert(ctx.rx_pos == 3u);
    assert(out_len == 0u);
    printf("PASSED\n");
}

static void test_echo_off_backspace_is_silent(void)
{
    printf("Test: echo off prints nothing for an erase... ");
    reset(0u);
    feed("ab\x08");
    assert(ctx.rx_pos == 1u);
    assert(strcmp(rx, "a") == 0);
    assert(out_len == 0u);
    printf("PASSED\n");
}

static void test_echo_off_still_submits(void)
{
    printf("Test: echo off still emits the terminator and the prompt... ");
    reset(0u);
    feed("led\n");
    assert(strcmp(out, "\n> ") == 0);
    printf("PASSED\n");
}

static void test_echo_off_recall_is_silent(void)
{
    printf("Test: echo off recalls without printing the line... ");
    reset(0u);
    feed("led on\n");
    out_clear();
    feed("\x1b[A");
    assert(strcmp(rx, "led on") == 0);
    assert(out_len == 0u);
    printf("PASSED\n");
}

static void test_echo_off_completion_is_silent(void)
{
    printf("Test: echo off completes the line without printing... ");
    reset(0u);
    feed("le\x09");
    assert(strcmp(rx, "led ") == 0);
    assert(out_len == 0u);
    printf("PASSED\n");
}

static void test_echo_off_unknown_still_reported(void)
{
    printf("Test: echo off still reports an unknown command... ");
    reset(0u);
    feed("nope\n");
    assert(strcmp(out, "unknown command: nope\n> ") == 0);
    printf("PASSED\n");
}

static void test_echo_on_unknown_order(void)
{
    printf("Test: echo on prints the line first, then the complaint... ");
    reset(ELIB_CLI_FLAG_ECHO);
    feed("nope\n");
    assert(strcmp(out, "nopeunknown command: nope\n> ") == 0);
    printf("PASSED\n");
}

static void test_echo_off_never_emits_backspace(void)
{
    printf("Test: echo off never emits a backspace... ");
    reset(0u);
    feed("abc\x08\x08\x7f");
    feed("\x1b[A");
    feed("\x09");
    assert(strchr(out, '\b') == NULL);
    printf("PASSED\n");
}

static void test_echo_on_multi_match_listing(void)
{
    printf("Test: echo on redraws prompt and line after a listing... ");
    reset(ELIB_CLI_FLAG_ECHO);
    feed("l\x09");
    assert(strcmp(out, "l\nled\nlink\n> l") == 0);
    printf("PASSED\n");
}

static void test_echo_flag_does_not_change_content(void)
{
    printf("Test: echo only changes what is printed, not the line... ");
    reset(ELIB_CLI_FLAG_ECHO);
    feed("led on\x08\x08n");
    {
        char with_echo[RX_SIZE];

        snprintf(with_echo, sizeof(with_echo), "%s", rx);
        reset(0u);
        feed("led on\x08\x08n");
        assert(strcmp(rx, with_echo) == 0);
        assert(strcmp(rx, "led n") == 0);
    }
    printf("PASSED\n");
}

int main(void)
{
    printf("=== test_echo ===\n");
    test_echo_on_types();
    test_echo_on_backspace();
    test_echo_on_submit();
    test_echo_off_is_silent_while_typing();
    test_echo_off_backspace_is_silent();
    test_echo_off_still_submits();
    test_echo_off_recall_is_silent();
    test_echo_off_completion_is_silent();
    test_echo_off_unknown_still_reported();
    test_echo_on_unknown_order();
    test_echo_off_never_emits_backspace();
    test_echo_on_multi_match_listing();
    test_echo_flag_does_not_change_content();
    printf("=== test_echo: all PASSED ===\n");
    return 0;
}
