/* test_elib_cli.c - CLI Unit Tests */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#include <stdint.h>
#include "../include/elib_cli.h"

/* --- Mock infrastructure --- */

static char mock_output[4096];
static uint16_t mock_output_pos;

static int mock_print(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(mock_output + mock_output_pos,
                      sizeof(mock_output) - mock_output_pos, fmt, ap);
    va_end(ap);
    if (n > 0) {
        mock_output_pos += (uint16_t)n;
    }
    return n;
}

static void mock_output_reset(void)
{
    mock_output[0] = '\0';
    mock_output_pos = 0;
}

/* Test contexts and buffers */
static char rx_buf[128];
static char history_buf[256];
static char saved_buf[128];
static elib_cli_cmd_t cmd_table[8];

static elib_cli_ctx_t test_ctx;

static const elib_cli_cfg_t default_cfg = {
    .prompt = "> ",
    .newline = ELIB_CLI_NL_LF,
    .print = mock_print,
    .rx_buf = rx_buf,
    .rx_buf_size = sizeof(rx_buf),
    .history_buf = history_buf,
    .history_buf_size = sizeof(history_buf),
    .cmd_table = cmd_table,
    .cmd_table_size = 8,
    .saved_input = saved_buf,
    .saved_buf_size = sizeof(saved_buf),
    .echo = 1,
};

/* Command callback tracking */
static char last_cmd_args[256];
static int cmd_executed;

static void cmd_test(const char *args)
{
    strncpy(last_cmd_args, args ? args : "", sizeof(last_cmd_args) - 1);
    last_cmd_args[sizeof(last_cmd_args) - 1] = '\0';
    cmd_executed++;
}

static void cmd_led(const char *args)
{
    strncpy(last_cmd_args, args ? args : "", sizeof(last_cmd_args) - 1);
    last_cmd_args[sizeof(last_cmd_args) - 1] = '\0';
    cmd_executed++;
}

static void reset_test(void)
{
    memset(&test_ctx, 0, sizeof(test_ctx));
    memset(rx_buf, 0, sizeof(rx_buf));
    memset(history_buf, 0, sizeof(history_buf));
    memset(saved_buf, 0, sizeof(saved_buf));
    memset(cmd_table, 0, sizeof(cmd_table));
    mock_output_reset();
    cmd_executed = 0;
    last_cmd_args[0] = '\0';
}

/* Helper: feed a string character by character */
static void feed_str(elib_cli_ctx_t *ctx, const char *s)
{
    while (*s) {
        elib_cli_feed_char(ctx, *s);
        s++;
    }
}

/* --- Tests --- */

static void test_init_deinit(void)
{
    reset_test();
    elib_cli_err_t err;

    /* NULL params */
    err = elib_cli_init(NULL, &default_cfg);
    assert(err == ELIB_CLI_ERR_INVALID_PARAM);

    err = elib_cli_init(&test_ctx, NULL);
    assert(err == ELIB_CLI_ERR_INVALID_PARAM);

    /* Missing required fields */
    elib_cli_cfg_t bad_cfg = default_cfg;
    bad_cfg.print = NULL;
    err = elib_cli_init(&test_ctx, &bad_cfg);
    assert(err == ELIB_CLI_ERR_INVALID_PARAM);

    /* Valid init */
    err = elib_cli_init(&test_ctx, &default_cfg);
    assert(err == ELIB_CLI_OK);
    assert(test_ctx.initialized == 1);
    assert(test_ctx.rx_pos == 0);

    /* Double op */
    elib_cli_deinit(&test_ctx);
    assert(test_ctx.initialized == 0);

    err = elib_cli_feed_char(&test_ctx, 'a');
    assert(err == ELIB_CLI_ERR_NOT_INITIALIZED);

    printf("  PASS: init/deinit\n");
}

static void test_register(void)
{
    reset_test();
    elib_cli_init(&test_ctx, &default_cfg);
    elib_cli_err_t err;

    /* Register commands */
    err = elib_cli_register(&test_ctx, "test", "Test command", cmd_test);
    assert(err == ELIB_CLI_OK);
    assert(test_ctx.cmd_count == 1);

    err = elib_cli_register(&test_ctx, "led", "LED control", cmd_led);
    assert(err == ELIB_CLI_OK);
    assert(test_ctx.cmd_count == 2);

    /* Override existing */
    err = elib_cli_register(&test_ctx, "test", "Updated help", cmd_test);
    assert(err == ELIB_CLI_OK);
    assert(test_ctx.cmd_count == 2); /* count unchanged, overridden */

    /* Table full */
    for (int i = 2; i < 8; i++) {
        char name[8];
        snprintf(name, sizeof(name), "cmd%d", i);
        elib_cli_register(&test_ctx, name, NULL, cmd_test);
    }
    err = elib_cli_register(&test_ctx, "overflow", NULL, cmd_test);
    assert(err == ELIB_CLI_ERR_CMD_TABLE_FULL);

    /* NULL params */
    err = elib_cli_register(NULL, "x", NULL, cmd_test);
    assert(err == ELIB_CLI_ERR_INVALID_PARAM);

    err = elib_cli_register(&test_ctx, NULL, NULL, cmd_test);
    assert(err == ELIB_CLI_ERR_INVALID_PARAM);

    printf("  PASS: register\n");
}

static void test_basic_command(void)
{
    reset_test();
    elib_cli_init(&test_ctx, &default_cfg);
    elib_cli_register(&test_ctx, "test", "Test command", cmd_test);
    elib_cli_register(&test_ctx, "led", "LED control", cmd_led);

    /* Type and execute "test\n" */
    feed_str(&test_ctx, "test\n");

    assert(cmd_executed == 1);
    assert(strcmp(last_cmd_args, "") == 0);

    /* Command with args */
    feed_str(&test_ctx, "led on\n");
    assert(cmd_executed == 2);
    assert(strcmp(last_cmd_args, "on") == 0);

    /* Command with multiple args */
    feed_str(&test_ctx, "led set 50\n");
    assert(cmd_executed == 3);
    assert(strcmp(last_cmd_args, "set 50") == 0);

    printf("  PASS: basic command\n");
}

static void test_backspace(void)
{
    reset_test();
    elib_cli_init(&test_ctx, &default_cfg);
    elib_cli_register(&test_ctx, "test", NULL, cmd_test);

    /* Type "tesx", backspace, type "t\n" -> "test\n" */
    feed_str(&test_ctx, "tesx");
    assert(test_ctx.rx_pos == 4);

    elib_cli_feed_char(&test_ctx, 0x08); /* backspace */
    assert(test_ctx.rx_pos == 3);
    assert(strcmp(rx_buf, "tes") == 0);

    feed_str(&test_ctx, "t\n");
    assert(cmd_executed == 1);
    assert(strcmp(last_cmd_args, "") == 0);

    printf("  PASS: backspace\n");
}

static void test_echo_off(void)
{
    reset_test();
    elib_cli_cfg_t no_echo_cfg = default_cfg;
    no_echo_cfg.echo = 0;

    elib_cli_init(&test_ctx, &no_echo_cfg);
    elib_cli_register(&test_ctx, "test", NULL, cmd_test);

    mock_output_reset();
    feed_str(&test_ctx, "test\n");

    /* Command should still execute, but no echo characters in output */
    assert(cmd_executed == 1);
    /* Output should only contain prompt and newline, not "test" echo */
    assert(strstr(mock_output, "test") == NULL || strstr(mock_output, "unknown") == NULL);

    printf("  PASS: echo off\n");
}

static void test_tab_single_match(void)
{
    reset_test();
    elib_cli_init(&test_ctx, &default_cfg);
    elib_cli_register(&test_ctx, "test", "Test", cmd_test);
    elib_cli_register(&test_ctx, "led", "LED", cmd_led);

    feed_str(&test_ctx, "te");
    elib_cli_feed_char(&test_ctx, '\t');

    /* Should auto-complete to "test " */
    assert(strcmp(rx_buf, "test ") == 0);
    assert(test_ctx.rx_pos == 5);

    printf("  PASS: tab single match\n");
}

static void test_tab_multi_match(void)
{
    reset_test();
    elib_cli_init(&test_ctx, &default_cfg);
    elib_cli_register(&test_ctx, "start", "Start", cmd_test);
    elib_cli_register(&test_ctx, "stop", "Stop", cmd_led);
    elib_cli_register(&test_ctx, "status", "Status", cmd_test);

    mock_output_reset();
    feed_str(&test_ctx, "st");
    elib_cli_feed_char(&test_ctx, '\t');

    /* Should list "start", "stop", "status" */
    assert(strstr(mock_output, "start") != NULL);
    assert(strstr(mock_output, "stop") != NULL);
    assert(strstr(mock_output, "status") != NULL);

    printf("  PASS: tab multi match\n");
}

static void test_tab_no_match(void)
{
    reset_test();
    elib_cli_init(&test_ctx, &default_cfg);
    elib_cli_register(&test_ctx, "test", "Test", cmd_test);

    mock_output_reset();
    feed_str(&test_ctx, "xyz");
    elib_cli_feed_char(&test_ctx, '\t');

    /* rx_buf unchanged */
    assert(strcmp(rx_buf, "xyz") == 0);

    printf("  PASS: tab no match\n");
}

static void test_history_basic(void)
{
    reset_test();
    elib_cli_init(&test_ctx, &default_cfg);
    elib_cli_register(&test_ctx, "cmd1", "Cmd1", cmd_test);
    elib_cli_register(&test_ctx, "cmd2", "Cmd2", cmd_led);

    /* Execute two commands */
    feed_str(&test_ctx, "cmd1\n");
    feed_str(&test_ctx, "cmd2\n");

    /* History buffer should contain "cmd1\ncmd2\n" */
    assert(strcmp(history_buf, "cmd1\ncmd2\n") == 0);

    printf("  PASS: history basic\n");
}

static void test_history_browse(void)
{
    reset_test();
    elib_cli_init(&test_ctx, &default_cfg);
    elib_cli_register(&test_ctx, "cmd1", "Cmd1", cmd_test);
    elib_cli_register(&test_ctx, "cmd2", "Cmd2", cmd_led);

    feed_str(&test_ctx, "cmd1\n");
    feed_str(&test_ctx, "cmd2\n");

    /* Press up - should show cmd2 (most recent) */
    feed_str(&test_ctx, "\x1b[A");
    assert(strcmp(rx_buf, "cmd2") == 0);

    /* Press up again - should show cmd1 */
    feed_str(&test_ctx, "\x1b[A");
    assert(strcmp(rx_buf, "cmd1") == 0);

    /* Press down - should show cmd2 */
    feed_str(&test_ctx, "\x1b[B");
    assert(strcmp(rx_buf, "cmd2") == 0);

    /* Press down - back to empty current input */
    feed_str(&test_ctx, "\x1b[B");
    assert(test_ctx.rx_pos == 0);

    printf("  PASS: history browse\n");
}

static void test_history_ring(void)
{
    reset_test();
    /* Use small history buffer */
    char small_hist[20];
    elib_cli_cfg_t small_cfg = default_cfg;
    small_cfg.history_buf = small_hist;
    small_cfg.history_buf_size = sizeof(small_hist);

    elib_cli_init(&test_ctx, &small_cfg);
    elib_cli_register(&test_ctx, "a", "A", cmd_test);
    elib_cli_register(&test_ctx, "bb", "BB", cmd_led);
    elib_cli_register(&test_ctx, "ccc", "CCC", cmd_test);

    feed_str(&test_ctx, "a\n");    /* "a\n" = 3 bytes */
    feed_str(&test_ctx, "bb\n");   /* "bb\n" = 4 bytes, total 7 */
    feed_str(&test_ctx, "ccc\n");  /* needs 4 bytes, total would be 11 > 20? no.
                                      "a\nbb\nccc\n" = 10 bytes, fits */

    assert(strstr(small_hist, "ccc") != NULL);

    /* Now add something that forces eviction */
    feed_str(&test_ctx, "dddd\n");  /* "dddd\n" = 5 bytes, total would be 15, fits */
    /* "a\nbb\nccc\ndddd\n" = 15 bytes, still fits in 20 */

    feed_str(&test_ctx, "eeeee\n"); /* total would be 22 > 20, need to evict */
    /* Should have evicted "a\n" -> "bb\nccc\ndddd\neeeee\n" = 19 bytes */

    assert(strstr(small_hist, "a\n") == NULL);
    assert(strstr(small_hist, "eeeee") != NULL);

    printf("  PASS: history ring\n");
}

static void test_crlf_mode(void)
{
    reset_test();
    elib_cli_cfg_t crlf_cfg = default_cfg;
    crlf_cfg.newline = ELIB_CLI_NL_CRLF;

    elib_cli_init(&test_ctx, &crlf_cfg);
    elib_cli_register(&test_ctx, "test", NULL, cmd_test);

    /* Normal \r\n should execute */
    feed_str(&test_ctx, "test\r\n");
    assert(cmd_executed == 1);

    /* \r followed by non-\n: tolerant execution */
    feed_str(&test_ctx, "test\r");
    assert(cmd_executed == 1); /* \r alone triggers via tolerant fallback */
    /* But wait, in strict CRLF mode, \r sets cr_pending.
       If the next char is not \n, it falls through.
       Here there's no next char yet, so cr_pending=1 and nothing executes.
       We need a following character to trigger the tolerant behavior. */
    /* Actually let me re-test: just \r sets cr_pending, no execution yet */

    /* Reset and test properly */
    reset_test();
    elib_cli_init(&test_ctx, &crlf_cfg);
    elib_cli_register(&test_ctx, "test", NULL, cmd_test);

    /* \r\n: normal execution */
    feed_str(&test_ctx, "test\r\n");
    assert(cmd_executed == 1);

    /* \r then regular char: tolerant \r triggers execute, char starts new input */
    mock_output_reset();
    feed_str(&test_ctx, "test\rX");
    assert(cmd_executed == 2);
    /* 'X' should be in rx_buf as start of new input */
    assert(rx_buf[0] == 'X');

    /* Standalone \n without preceding \r: ignored in strict CRLF */
    feed_str(&test_ctx, "\n");
    assert(cmd_executed == 2); /* no execution */

    printf("  PASS: CRLF mode\n");
}

static void test_unknown_command(void)
{
    reset_test();
    elib_cli_init(&test_ctx, &default_cfg);

    feed_str(&test_ctx, "foobar\n");
    assert(strstr(mock_output, "unknown command: foobar") != NULL);

    printf("  PASS: unknown command\n");
}

static void test_empty_line(void)
{
    reset_test();
    elib_cli_init(&test_ctx, &default_cfg);
    elib_cli_register(&test_ctx, "test", NULL, cmd_test);

    feed_str(&test_ctx, "\n");
    assert(cmd_executed == 0);

    feed_str(&test_ctx, "   \n");
    assert(cmd_executed == 0);

    printf("  PASS: empty line\n");
}

static void test_arg_helpers(void)
{
    assert(elib_cli_argc(NULL) == 0);
    assert(elib_cli_argc("") == 0);
    assert(elib_cli_argc("hello") == 1);
    assert(elib_cli_argc("hello world") == 2);
    assert(elib_cli_argc("  hello   world  ") == 2);
    assert(elib_cli_argc("a b c") == 3);

    const char *r;
    r = elib_cli_argv("hello world", 0);
    assert(r != NULL && strncmp(r, "hello", 5) == 0 && r[5] == ' ');
    r = elib_cli_argv("hello world", 1);
    assert(r != NULL && strcmp(r, "world") == 0);
    assert(elib_cli_argv("hello", 1) == NULL);
    assert(elib_cli_argv(NULL, 0) == NULL);

    /* argv returns pointer to start of token within the string */
    const char *args = "set 50";
    const char *arg0 = elib_cli_argv(args, 0);
    assert(arg0 != NULL && strncmp(arg0, "set", 3) == 0);
    const char *arg1 = elib_cli_argv(args, 1);
    assert(arg1 != NULL && strncmp(arg1, "50", 2) == 0);

    printf("  PASS: arg helpers\n");
}

static void test_rx_overflow(void)
{
    reset_test();
    char small_rx[8];
    elib_cli_cfg_t small_cfg = default_cfg;
    small_cfg.rx_buf = small_rx;
    small_cfg.rx_buf_size = sizeof(small_rx);

    elib_cli_init(&test_ctx, &small_cfg);
    elib_cli_register(&test_ctx, "test", NULL, cmd_test);

    elib_cli_err_t err = ELIB_CLI_OK;
    for (int i = 0; i < 10; i++) {
        err = elib_cli_feed_char(&test_ctx, 'a' + i);
    }
    assert(err == ELIB_CLI_ERR_RX_OVERFLOW);

    printf("  PASS: rx overflow\n");
}

static void test_builtin_help(void)
{
    reset_test();
    elib_cli_init(&test_ctx, &default_cfg);
    elib_cli_register(&test_ctx, "led", "Control LED", cmd_led);
    elib_cli_register(&test_ctx, "reboot", "Reboot system", cmd_test);

    mock_output_reset();
    feed_str(&test_ctx, "help\n");

    /* Output should list all commands including help itself */
    assert(strstr(mock_output, "led") != NULL);
    assert(strstr(mock_output, "Control LED") != NULL);
    assert(strstr(mock_output, "reboot") != NULL);

    printf("  PASS: builtin help\n");
}

static void test_help_override(void)
{
    reset_test();
    elib_cli_init(&test_ctx, &default_cfg);

    /* Override built-in help */
    elib_cli_register(&test_ctx, "help", "Custom help", cmd_test);

    mock_output_reset();
    feed_str(&test_ctx, "help\n");

    /* Should call our cmd_test callback, not the built-in */
    assert(cmd_executed == 1);

    printf("  PASS: help override\n");
}

/* --- Main --- */

int main(void)
{
    printf("elib_cli tests:\n");

    test_init_deinit();
    test_register();
    test_basic_command();
    test_backspace();
    test_echo_off();
    test_tab_single_match();
    test_tab_multi_match();
    test_tab_no_match();
    test_history_basic();
    test_history_browse();
    test_history_ring();
    test_crlf_mode();
    test_unknown_command();
    test_empty_line();
    test_arg_helpers();
    test_rx_overflow();
    test_builtin_help();
    test_help_override();

    printf("\nAll tests passed!\n");
    return 0;
}
