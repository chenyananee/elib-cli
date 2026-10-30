/* test_execute.c --- dispatch, argument splitting, reply ordering, return codes */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include "elib_cli.h"

#define RX_SIZE     32u
#define HIST_SIZE   128u
#define TABLE_SIZE  4u
#define OUT_SIZE    256u

static elib_cli_ctx_t ctx;
static char rx[RX_SIZE];
static char hist[HIST_SIZE];
static elib_cli_cmd_t table[TABLE_SIZE];
static char out[OUT_SIZE];
static size_t out_len;
static elib_cli_print_fn g_print;
static void *g_cookie;
static int g_marker;
static unsigned g_calls;
static char g_args[RX_SIZE];
static int g_args_null;

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

static void on_led(void *ud, const char *args)
{
    g_calls++;
    g_cookie = ud;
    g_args_null = (args == NULL);
    if (args != NULL) {
        snprintf(g_args, sizeof(g_args), "%s", args);
    } else {
        g_args[0] = '\0';
    }
    /* Printing from the callback must land before the terminator */
    g_print(ud, "[%s]", args != NULL ? args : "NULL");
}

static void on_reboot(void *ud, const char *args)
{
    (void)ud;
    (void)args;
    g_calls++;
    g_print(NULL, "[reboot]");
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
    g_print = capture;
    g_cookie = NULL;
    g_marker = 0;
    g_calls = 0u;
    g_args[0] = '\0';
    g_args_null = 0;

    cfg.prompt = "> ";
    cfg.newline = ELIB_CLI_NL_LF;
    cfg.print = capture;
    cfg.user_data = &g_marker;
    cfg.rx_buf = rx;
    cfg.rx_buf_size = RX_SIZE;
    cfg.history_buf = hist;
    cfg.history_buf_size = HIST_SIZE;
    cfg.cmd_table = table;
    cfg.cmd_table_size = TABLE_SIZE;
    cfg.flags = ELIB_CLI_FLAG_ECHO;
    assert(elib_cli_init(&ctx, &cfg) == ELIB_CLI_OK);
    assert(elib_cli_register(&ctx, "led", "Control LED", on_led) == ELIB_CLI_OK);
    assert(elib_cli_register(&ctx, "reboot", NULL, on_reboot) == ELIB_CLI_OK);
}

static void feed(const char *s)
{
    while (*s != '\0') {
        (void)elib_cli_feed_char(&ctx, *s);
        s++;
    }
}

static void test_dispatch_without_args(void)
{
    printf("Test: a bare command gets an empty, non-NULL argument string... ");
    reset();
    assert(elib_cli_feed_char(&ctx, 'l') == ELIB_CLI_OK);
    feed("ed\n");

    assert(g_calls == 1u);
    assert(g_args_null == 0);
    assert(strcmp(g_args, "") == 0);
    assert(strcmp(out, "led[]\n> ") == 0);
    printf("PASSED\n");
}

static void test_dispatch_with_args(void)
{
    printf("Test: the text after the name is passed verbatim... ");
    reset();
    feed("led on\n");
    assert(g_calls == 1u);
    assert(strcmp(g_args, "on") == 0);
    assert(strcmp(out, "led on[on]\n> ") == 0);
    printf("PASSED\n");
}

static void test_args_keep_inner_spacing(void)
{
    printf("Test: inner and trailing spaces survive, leading ones do not... ");
    reset();
    feed("led   a  b \n");
    assert(g_calls == 1u);
    assert(strcmp(g_args, "a  b ") == 0);
    assert(strcmp(out, "led   a  b [a  b ]\n> ") == 0);
    printf("PASSED\n");
}

static void test_leading_spaces_are_stripped(void)
{
    printf("Test: leading spaces are not part of the command name... ");
    reset();
    feed("   led\n");
    assert(g_calls == 1u);
    assert(strcmp(g_args, "") == 0);
    assert(strcmp(out, "   led[]\n> ") == 0);
    printf("PASSED\n");
}

static void test_empty_line(void)
{
    printf("Test: an empty line just prints a fresh prompt... ");
    reset();
    assert(elib_cli_feed_char(&ctx, '\n') == ELIB_CLI_OK);
    assert(g_calls == 0u);
    assert(strcmp(out, "\n> ") == 0);
    assert(strcmp(hist, "") == 0);
    assert(ctx.rx_pos == 0u);
    printf("PASSED\n");
}

static void test_blank_line_is_empty(void)
{
    printf("Test: a line of spaces counts as empty and is not stored... ");
    reset();
    feed("   \n");
    assert(g_calls == 0u);
    assert(strcmp(out, "   \n> ") == 0);
    assert(strcmp(hist, "") == 0);
    printf("PASSED\n");
}

static void test_unknown_command(void)
{
    printf("Test: an unknown command names itself and reports CMD_NOT_FOUND... ");
    reset();
    assert(elib_cli_feed_char(&ctx, 'f') == ELIB_CLI_OK);
    feed("oo bar\n");
    assert(g_calls == 0u);
    assert(strcmp(out, "foo barunknown command: foo\n> ") == 0);
    assert(strcmp(hist, "foo bar\n") == 0);   /* full line kept in history */
    printf("PASSED\n");
}

static void test_unknown_report(void)
{
    printf("Test: feed_char returns CMD_NOT_FOUND for the submitting byte... ");
    reset();
    feed("nope");
    assert(elib_cli_feed_char(&ctx, '\n') == ELIB_CLI_ERR_CMD_NOT_FOUND);
    printf("PASSED\n");
}

static void test_known_returns_ok(void)
{
    printf("Test: submitting a known command returns OK... ");
    reset();
    feed("reboot");
    assert(elib_cli_feed_char(&ctx, '\n') == ELIB_CLI_OK);
    assert(strcmp(out, "reboot[reboot]\n> ") == 0);
    printf("PASSED\n");
}

static void test_names_are_case_sensitive(void)
{
    printf("Test: an upper-case name is not a match... ");
    reset();
    feed("LED\n");
    assert(g_calls == 0u);
    assert(strcmp(out, "LEDunknown command: LED\n> ") == 0);
    printf("PASSED\n");
}

static void test_user_data_cookie(void)
{
    printf("Test: the callback receives the configured user_data... ");
    reset();
    feed("led\n");
    assert(g_cookie == &g_marker);
    printf("PASSED\n");
}

static void test_state_is_reset_after_submit(void)
{
    printf("Test: submitting clears the line and the history cursor... ");
    reset();
    feed("led on\n");
    assert(ctx.rx_pos == 0u);
    assert(rx[0] == '\0');
    assert(ctx.hist_offset == 0);

    out_len = 0u;
    out[0] = '\0';
    feed("led\n");
    assert(strcmp(out, "led[]\n> ") == 0);
    printf("PASSED\n");
}

static void test_history_keeps_arguments(void)
{
    printf("Test: history stores the whole line, arguments included... ");
    reset();
    feed("led on\n");
    assert(strcmp(hist, "led on\n") == 0);
    feed("led\n");
    assert(strcmp(hist, "led on\nled\n") == 0);
    printf("PASSED\n");
}

static void test_callback_may_print(void)
{
    printf("Test: what a callback prints lands before the terminator... ");
    reset();
    feed("reboot\n");
    assert(strcmp(out, "reboot[reboot]\n> ") == 0);
    printf("PASSED\n");
}

static void test_second_command_after_first(void)
{
    printf("Test: consecutive lines dispatch independently... ");
    reset();
    feed("led one\n");
    feed("reboot\n");
    feed("led two\n");
    assert(g_calls == 3u);
    assert(strcmp(g_args, "two") == 0);
    assert(strcmp(out,
                  "led one[one]\n> reboot[reboot]\n> led two[two]\n> ") == 0);
    printf("PASSED\n");
}

int main(void)
{
    printf("=== test_execute ===\n");
    test_dispatch_without_args();
    test_dispatch_with_args();
    test_args_keep_inner_spacing();
    test_leading_spaces_are_stripped();
    test_empty_line();
    test_blank_line_is_empty();
    test_unknown_command();
    test_unknown_report();
    test_known_returns_ok();
    test_names_are_case_sensitive();
    test_user_data_cookie();
    test_state_is_reset_after_submit();
    test_history_keeps_arguments();
    test_callback_may_print();
    test_second_command_after_first();
    printf("=== test_execute: all PASSED ===\n");
    return 0;
}
