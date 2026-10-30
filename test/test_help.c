/* test_help.c --- the built-in help listing, its slotless nature and override */
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

static void reset(uint32_t flags)
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
    cfg.newline = ELIB_CLI_NL_LF;
    cfg.print = capture;
    cfg.rx_buf = rx;
    cfg.rx_buf_size = RX_SIZE;
    cfg.history_buf = hist;
    cfg.history_buf_size = HIST_SIZE;
    cfg.cmd_table = table;
    cfg.cmd_table_size = TABLE_SIZE;
    cfg.flags = flags;
    assert(elib_cli_init(&ctx, &cfg) == ELIB_CLI_OK);
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

static void test_help_lists_table_then_itself(void)
{
    printf("Test: help lists the table, then itself... ");
    reset(ELIB_CLI_FLAG_ECHO);
    assert(elib_cli_register(&ctx, "led", "Control LED", on_cmd)
           == ELIB_CLI_OK);
    assert(elib_cli_register(&ctx, "reboot", NULL, on_cmd) == ELIB_CLI_OK);

    assert(elib_cli_feed_char(&ctx, 'h') == ELIB_CLI_OK);
    feed("elp\n");
    assert(calls == 0u);
    assert(strcmp(out,
                  "help"
                  "led          Control LED\n"
                  "reboot\n"
                  "help         Show available commands"
                  "\n> ") == 0);
    printf("PASSED\n");
}

static void test_help_without_commands(void)
{
    printf("Test: help with an empty table lists only itself... ");
    reset(ELIB_CLI_FLAG_ECHO);
    feed("help\n");
    assert(strcmp(out, "helphelp         Show available commands\n> ") == 0);
    printf("PASSED\n");
}

static void test_help_does_not_consume_a_slot(void)
{
    printf("Test: the built-in help does not occupy a table slot... ");
    reset(ELIB_CLI_FLAG_ECHO);
    assert(ctx.cmd_count == 0u);
    feed("help\n");
    assert(ctx.cmd_count == 0u);

    assert(elib_cli_register(&ctx, "a", NULL, on_cmd) == ELIB_CLI_OK);
    feed("help\n");
    assert(ctx.cmd_count == 1u);
    printf("PASSED\n");
}

static void test_help_ignores_arguments(void)
{
    printf("Test: help ignores its arguments... ");
    reset(ELIB_CLI_FLAG_ECHO);
    out_clear();
    feed("help whatever\n");
    assert(strcmp(out,
                  "help whatever"
                  "help         Show available commands\n> ") == 0);
    printf("PASSED\n");
}

static void test_help_returns_ok(void)
{
    printf("Test: help reports success... ");
    reset(ELIB_CLI_FLAG_ECHO);
    feed("help");
    assert(elib_cli_feed_char(&ctx, '\n') == ELIB_CLI_OK);
    printf("PASSED\n");
}

static void test_help_can_be_overridden(void)
{
    printf("Test: registering help replaces the built-in listing... ");
    reset(ELIB_CLI_FLAG_ECHO);
    assert(elib_cli_register(&ctx, "help", "mine", on_cmd) == ELIB_CLI_OK);
    assert(ctx.cmd_count == 1u);
    assert(strcmp(ctx.cfg.cmd_table[0].help, "mine") == 0);

    feed("help\n");
    assert(calls == 1u);
    assert(strcmp(out, "help\n> ") == 0);  /* no listing was printed */
    printf("PASSED\n");
}

static void test_help_override_with_echo_off(void)
{
    printf("Test: an overridden help behaves like any other command... ");
    reset(0u);
    assert(elib_cli_register(&ctx, "help", NULL, on_cmd) == ELIB_CLI_OK);
    feed("help\n");
    assert(calls == 1u);
    assert(strcmp(out, "\n> ") == 0);
    printf("PASSED\n");
}

static void test_help_with_echo_off(void)
{
    printf("Test: the built-in listing is printed even with echo off... ");
    reset(0u);
    feed("help\n");
    assert(strcmp(out, "help         Show available commands\n> ") == 0);
    printf("PASSED\n");
}

static void test_help_line_termination(void)
{
    printf("Test: the listing has no trailing blank line... ");
    reset(ELIB_CLI_FLAG_ECHO);
    assert(elib_cli_register(&ctx, "a", NULL, on_cmd) == ELIB_CLI_OK);
    feed("help\n");

    /* exactly three terminators: after "a", after the last entry, and the one
     * that ends the submitted line */
    {
        const char *p = out;
        unsigned newlines = 0u;

        while (*p != '\0') {
            if (*p == '\n') {
                newlines++;
            }
            p++;
        }
        assert(newlines == 2u);
    }
    printf("PASSED\n");
}

static void test_help_is_case_sensitive(void)
{
    printf("Test: only the lower-case help is built in... ");
    reset(ELIB_CLI_FLAG_ECHO);
    feed("HELP\n");
    assert(strcmp(out, "HELPunknown command: HELP\n> ") == 0);
    printf("PASSED\n");
}

static void test_help_after_a_command(void)
{
    printf("Test: help works after other commands ran... ");
    reset(ELIB_CLI_FLAG_ECHO);
    assert(elib_cli_register(&ctx, "led", "Control LED", on_cmd)
           == ELIB_CLI_OK);
    feed("led\n");
    assert(calls == 1u);
    out_clear();
    feed("help\n");
    assert(strcmp(out,
                  "help"
                  "led          Control LED\n"
                  "help         Show available commands"
                  "\n> ") == 0);
    printf("PASSED\n");
}

static void test_help_entry_alignment(void)
{
    printf("Test: entries are aligned in a 12-column name field... ");
    reset(0u);
    assert(elib_cli_register(&ctx, "x", "X help", on_cmd) == ELIB_CLI_OK);
    assert(elib_cli_register(&ctx, "abcdefghijkl", "long", on_cmd)
           == ELIB_CLI_OK);
    feed("help\n");

    /* a short name is padded, a name of exactly 12 characters is not */
    assert(strcmp(out,
                  "x            X help\n"
                  "abcdefghijkl long\n"
                  "help         Show available commands"
                  "\n> ") == 0);
    printf("PASSED\n");
}

int main(void)
{
    printf("=== test_help ===\n");
    test_help_lists_table_then_itself();
    test_help_without_commands();
    test_help_does_not_consume_a_slot();
    test_help_ignores_arguments();
    test_help_returns_ok();
    test_help_can_be_overridden();
    test_help_override_with_echo_off();
    test_help_with_echo_off();
    test_help_line_termination();
    test_help_is_case_sensitive();
    test_help_after_a_command();
    test_help_entry_alignment();
    printf("=== test_help: all PASSED ===\n");
    return 0;
}
