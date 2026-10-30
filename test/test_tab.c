/* test_tab.c --- command completion, listing, builtin help, capacity limits */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include "elib_cli.h"

#define RX_STORAGE  64u
#define HIST_SIZE   128u
#define TABLE_SIZE  8u
#define OUT_SIZE    512u

static elib_cli_ctx_t ctx;
static char rx[RX_STORAGE];
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

static void on_cmd(void *ud, const char *args) { (void)ud; (void)args; }

static void reset(uint32_t rx_size)
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
    cfg.rx_buf_size = rx_size;
    cfg.history_buf = hist;
    cfg.history_buf_size = HIST_SIZE;
    cfg.cmd_table = table;
    cfg.cmd_table_size = TABLE_SIZE;
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

static void test_single_match(void)
{
    printf("Test: a single match completes the name and appends a space... ");
    reset(RX_STORAGE);
    assert(elib_cli_register(&ctx, "led", NULL, on_cmd) == ELIB_CLI_OK);

    feed("l\t");
    assert(strcmp(rx, "led ") == 0);
    assert(ctx.rx_pos == 4u);
    assert(strcmp(out, "l\b \bled ") == 0);
    printf("PASSED\n");
}

static void test_no_match(void)
{
    printf("Test: an unmatched prefix is left alone and prints nothing... ");
    reset(RX_STORAGE);
    assert(elib_cli_register(&ctx, "led", NULL, on_cmd) == ELIB_CLI_OK);

    feed("xy\t");
    assert(strcmp(rx, "xy") == 0);
    assert(strcmp(out, "xy") == 0);
    printf("PASSED\n");
}

static void test_empty_line(void)
{
    printf("Test: TAB on an empty line does nothing... ");
    reset(RX_STORAGE);
    assert(elib_cli_register(&ctx, "led", NULL, on_cmd) == ELIB_CLI_OK);

    assert(elib_cli_feed_char(&ctx, '\t') == ELIB_CLI_OK);
    assert(ctx.rx_pos == 0u);
    assert(out_len == 0u);
    printf("PASSED\n");
}

static void test_multiple_matches_list(void)
{
    printf("Test: several matches are listed and the line is redrawn... ");
    reset(RX_STORAGE);
    assert(elib_cli_register(&ctx, "start", NULL, on_cmd) == ELIB_CLI_OK);
    assert(elib_cli_register(&ctx, "stop", NULL, on_cmd) == ELIB_CLI_OK);
    assert(elib_cli_register(&ctx, "status", NULL, on_cmd) == ELIB_CLI_OK);

    feed("st\t");
    assert(strcmp(rx, "st") == 0);              /* the line is untouched */
    assert(strcmp(out, "st\nstart\nstop\nstatus\n> st") == 0);
    printf("PASSED\n");
}

static void test_multiple_matches_include_builtin(void)
{
    printf("Test: the built-in help is listed with the other matches... ");
    reset(RX_STORAGE);
    assert(elib_cli_register(&ctx, "hello", NULL, on_cmd) == ELIB_CLI_OK);

    feed("he\t");
    assert(strcmp(rx, "he") == 0);
    assert(strcmp(out, "he\nhello\nhelp\n> he") == 0);
    printf("PASSED\n");
}

static void test_builtin_help_completion(void)
{
    printf("Test: TAB reaches the built-in help... ");
    reset(RX_STORAGE);

    feed("hel\t");
    assert(strcmp(rx, "help ") == 0);
    assert(strcmp(out, "hel\b \b\b \b\b \bhelp ") == 0);
    printf("PASSED\n");
}

static void test_tab_after_space_does_nothing(void)
{
    printf("Test: TAB in the argument area leaves the line alone... ");
    reset(RX_STORAGE);
    assert(elib_cli_register(&ctx, "led", NULL, on_cmd) == ELIB_CLI_OK);

    feed("led \t");
    assert(strcmp(rx, "led ") == 0);
    assert(strcmp(out, "led ") == 0);
    printf("PASSED\n");
}

static void test_tab_after_argument_does_nothing(void)
{
    printf("Test: TAB after a full argument list still does nothing... ");
    reset(RX_STORAGE);
    assert(elib_cli_register(&ctx, "led", NULL, on_cmd) == ELIB_CLI_OK);

    feed("led on\t");
    assert(strcmp(rx, "led on") == 0);
    assert(strcmp(out, "led on") == 0);
    printf("PASSED\n");
}

static void test_completion_respects_capacity(void)
{
    printf("Test: a completion that cannot fit is skipped... ");
    reset(5u);                                  /* room for 4 characters */
    assert(elib_cli_register(&ctx, "status", NULL, on_cmd) == ELIB_CLI_OK);

    feed("s\t");
    assert(strcmp(rx, "s") == 0);               /* "status " needs 8 bytes */
    assert(strcmp(out, "s") == 0);
    printf("PASSED\n");
}

static void test_completion_fits_exactly(void)
{
    printf("Test: a completion that fits exactly is applied... ");
    reset(6u);                                  /* room for 5 characters */
    assert(elib_cli_register(&ctx, "led", NULL, on_cmd) == ELIB_CLI_OK);

    feed("l\t");
    assert(strcmp(rx, "led ") == 0);
    printf("PASSED\n");
}

static void test_help_override_is_not_duplicated(void)
{
    printf("Test: a user help shadows the built-in, also for completion... ");
    reset(RX_STORAGE);
    assert(elib_cli_register(&ctx, "help", NULL, on_cmd) == ELIB_CLI_OK);

    feed("hel\t");
    assert(strcmp(rx, "help ") == 0);           /* single match, one listing */
    assert(strcmp(out, "hel\b \b\b \b\b \bhelp ") == 0);
    printf("PASSED\n");
}

static void test_completion_after_backspace(void)
{
    printf("Test: completion works on the edited prefix... ");
    reset(RX_STORAGE);
    assert(elib_cli_register(&ctx, "led", NULL, on_cmd) == ELIB_CLI_OK);

    feed("lex\x08\t");
    assert(strcmp(rx, "led ") == 0);
    assert(strcmp(out, "lex\b \b\b \b\b \bled ") == 0);
    printf("PASSED\n");
}

static void test_completion_with_echo_off(void)
{
    printf("Test: with echo off completion changes the line silently... ");
    reset(RX_STORAGE);
    {
        elib_cli_cfg_t cfg;

        memset(&cfg, 0, sizeof(cfg));
        memset(&ctx, 0, sizeof(ctx));
        memset(table, 0, sizeof(table));
        out_len = 0u;
        out[0] = '\0';
        cfg.prompt = "> ";
        cfg.newline = ELIB_CLI_NL_LF;
        cfg.print = capture;
        cfg.rx_buf = rx;
        cfg.rx_buf_size = RX_STORAGE;
        cfg.cmd_table = table;
        cfg.cmd_table_size = TABLE_SIZE;
        cfg.flags = 0u;
        assert(elib_cli_init(&ctx, &cfg) == ELIB_CLI_OK);
    }
    assert(elib_cli_register(&ctx, "led", NULL, on_cmd) == ELIB_CLI_OK);

    feed("l\t");
    assert(strcmp(rx, "led ") == 0);
    assert(out_len == 0u);                      /* no echo, no redraw */
    printf("PASSED\n");
}

static void test_listing_with_echo_off(void)
{
    printf("Test: a multi-match listing is printed even with echo off... ");
    reset(RX_STORAGE);
    {
        elib_cli_cfg_t cfg;

        memset(&cfg, 0, sizeof(cfg));
        memset(&ctx, 0, sizeof(ctx));
        memset(table, 0, sizeof(table));
        out_len = 0u;
        out[0] = '\0';
        cfg.prompt = "> ";
        cfg.newline = ELIB_CLI_NL_LF;
        cfg.print = capture;
        cfg.rx_buf = rx;
        cfg.rx_buf_size = RX_STORAGE;
        cfg.cmd_table = table;
        cfg.cmd_table_size = TABLE_SIZE;
        cfg.flags = 0u;
        assert(elib_cli_init(&ctx, &cfg) == ELIB_CLI_OK);
    }
    assert(elib_cli_register(&ctx, "go", NULL, on_cmd) == ELIB_CLI_OK);
    assert(elib_cli_register(&ctx, "get", NULL, on_cmd) == ELIB_CLI_OK);

    feed("g\t");
    assert(strcmp(out, "\ngo\nget\n> ") == 0);   /* no line echo after prompt */
    printf("PASSED\n");
}

int main(void)
{
    printf("=== test_tab ===\n");
    test_single_match();
    test_no_match();
    test_empty_line();
    test_multiple_matches_list();
    test_multiple_matches_include_builtin();
    test_builtin_help_completion();
    test_tab_after_space_does_nothing();
    test_tab_after_argument_does_nothing();
    test_completion_respects_capacity();
    test_completion_fits_exactly();
    test_help_override_is_not_duplicated();
    test_completion_after_backspace();
    test_completion_with_echo_off();
    test_listing_with_echo_off();
    printf("=== test_tab: all PASSED ===\n");
    return 0;
}
