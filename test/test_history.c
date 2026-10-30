/* test_history.c --- recording, browsing, saved input, eviction, limits */
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

static void reset_full(uint32_t hist_size, uint32_t saved_size)
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
    cfg.cmd_table = table;
    cfg.cmd_table_size = TABLE_SIZE;
    cfg.flags = ELIB_CLI_FLAG_ECHO;
    if (hist_size > 0u) {
        cfg.history_buf = hist;
        cfg.history_buf_size = hist_size;
    }
    if (saved_size > 0u) {
        cfg.saved_input = saved;
        cfg.saved_buf_size = saved_size;
    }
    assert(elib_cli_init(&ctx, &cfg) == ELIB_CLI_OK);
    assert(elib_cli_register(&ctx, "aaa", NULL, on_cmd) == ELIB_CLI_OK);
    assert(elib_cli_register(&ctx, "bbb", NULL, on_cmd) == ELIB_CLI_OK);
    assert(elib_cli_register(&ctx, "ccc", NULL, on_cmd) == ELIB_CLI_OK);
}

static void reset(void)
{
    reset_full(HIST_SIZE, SAVED_SIZE);
}

static void feed(const char *s)
{
    while (*s != '\0') {
        (void)elib_cli_feed_char(&ctx, *s);
        s++;
    }
}

static void up(void) { feed("\x1b[A"); }
static void down(void) { feed("\x1b[B"); }

static void out_clear(void)
{
    out_len = 0u;
    out[0] = '\0';
}

static void test_recording(void)
{
    printf("Test: each submitted line is appended with its terminator... ");
    reset();
    feed("aaa\n");
    assert(strcmp(hist, "aaa\n") == 0);
    feed("bbb\n");
    assert(strcmp(hist, "aaa\nbbb\n") == 0);
    printf("PASSED\n");
}

static void test_recording_keeps_arguments(void)
{
    printf("Test: the whole line, arguments included, is recorded... ");
    reset();
    feed("aaa one two\n");
    assert(strcmp(hist, "aaa one two\n") == 0);
    printf("PASSED\n");
}

static void test_recording_skips_empty_lines(void)
{
    printf("Test: an empty submit is not recorded... ");
    reset();
    feed("\n");
    assert(strcmp(hist, "") == 0);
    feed("   \n");
    assert(strcmp(hist, "") == 0);
    printf("PASSED\n");
}

static void test_up_recalls(void)
{
    printf("Test: up recalls the most recent line first... ");
    reset();
    feed("aaa\nbbb\n");
    out_clear();
    up();
    assert(strcmp(rx, "bbb") == 0);
    assert(ctx.rx_pos == 3u);
    assert(strcmp(out, "bbb") == 0);

    out_clear();
    up();
    assert(strcmp(rx, "aaa") == 0);
    assert(strcmp(out, "\b \b\b \b\b \baaa") == 0);
    printf("PASSED\n");
}

static void test_up_stops_at_oldest(void)
{
    printf("Test: up stops at the oldest entry... ");
    reset();
    feed("aaa\n");
    up();
    up();
    up();
    assert(strcmp(rx, "aaa") == 0);
    assert(ctx.hist_offset == 1);
    printf("PASSED\n");
}

static void test_up_redraws_the_line(void)
{
    printf("Test: recalling a line erases what was typed... ");
    reset();
    feed("aaa\n");
    feed("xy");
    out_clear();
    up();
    assert(strcmp(out, "\b \b\b \baaa") == 0);
    assert(strcmp(rx, "aaa") == 0);
    printf("PASSED\n");
}

static void test_down_restores_saved_input(void)
{
    printf("Test: down at the newest entry restores the typed line... ");
    reset();
    feed("aaa\n");
    feed("xy");
    up();
    out_clear();
    down();
    assert(strcmp(rx, "xy") == 0);
    assert(ctx.rx_pos == 2u);
    assert(strcmp(out, "\b \b\b \b\b \bxy") == 0);
    assert(ctx.hist_offset == 0);
    printf("PASSED\n");
}

static void test_down_walks_forward(void)
{
    printf("Test: down walks back towards the newest entry... ");
    reset();
    feed("aaa\nbbb\n");
    up();
    up();
    down();
    assert(strcmp(rx, "bbb") == 0);
    assert(ctx.hist_offset == 1);
    printf("PASSED\n");
}

static void test_down_at_zero_is_a_noop(void)
{
    printf("Test: down with nothing recalled does nothing... ");
    reset();
    feed("aaa\n");
    feed("xy");
    out_clear();
    down();
    assert(strcmp(rx, "xy") == 0);
    assert(out_len == 0u);
    printf("PASSED\n");
}

static void test_saved_input_is_truncated(void)
{
    printf("Test: a typed line longer than saved_input is truncated safely... ");
    reset_full(HIST_SIZE, 4u);                 /* room for 3 characters */
    feed("aaa\n");
    feed("abcdef");
    up();
    down();
    assert(strcmp(rx, "abc") == 0);
    assert(ctx.rx_pos == 3u);
    assert(strlen(saved) == 3u);
    printf("PASSED\n");
}

static void test_no_history_buffer(void)
{
    printf("Test: without a history buffer up and down do nothing... ");
    reset_full(0u, SAVED_SIZE);
    feed("aaa\n");
    assert(strcmp(hist, "") == 0);
    out_clear();
    up();
    assert(ctx.rx_pos == 0u);
    assert(out_len == 0u);
    printf("PASSED\n");
}

static void test_empty_history(void)
{
    printf("Test: up with an empty history prints nothing... ");
    reset();
    feed("xy");
    out_clear();
    up();
    assert(strcmp(rx, "xy") == 0);
    assert(out_len == 0u);
    printf("PASSED\n");
}

static void test_eviction_drops_whole_entries(void)
{
    printf("Test: a full history evicts the oldest entry whole... ");
    reset_full(10u, SAVED_SIZE);               /* "aaa\nbbb\n" = 8, +4 > 10 */
    feed("aaa\n");
    feed("bbb\n");
    assert(strcmp(hist, "aaa\nbbb\n") == 0);
    feed("ccc\n");
    assert(strcmp(hist, "bbb\nccc\n") == 0);
    printf("PASSED\n");
}

static void test_oversize_line_is_skipped(void)
{
    printf("Test: a line too long for the buffer does not destroy history... ");
    reset_full(6u, SAVED_SIZE);
    feed("aaa\n");                             /* "aaa\n" fits in 6 bytes */
    feed("abcdef\n");                          /* needs 8 bytes, skipped */
    assert(strcmp(hist, "aaa\n") == 0);
    printf("PASSED\n");
}

static void test_duplicates_are_kept(void)
{
    printf("Test: consecutive identical lines are stored twice... ");
    reset();
    feed("aaa\n");
    feed("aaa\n");
    assert(strcmp(hist, "aaa\naaa\n") == 0);
    up();
    assert(strcmp(rx, "aaa") == 0);
    up();
    assert(strcmp(rx, "aaa") == 0);
    printf("PASSED\n");
}

static void test_submit_resets_the_cursor(void)
{
    printf("Test: submitting resets the history cursor... ");
    reset();
    feed("aaa\nbbb\n");
    up();
    assert(ctx.hist_offset == 1);
    feed("\n");
    assert(ctx.hist_offset == 0);
    up();
    assert(strcmp(rx, "bbb") == 0);
    printf("PASSED\n");
}

static void test_history_is_not_terminated_by_partial_entry(void)
{
    printf("Test: the history buffer always ends on a newline... ");
    reset_full(12u, SAVED_SIZE);
    feed("aaa\n");
    feed("bbb\n");
    feed("ccc\n");
    {
        size_t len = strlen(hist);

        assert(len > 0u);
        assert(hist[len - 1u] == '\n');
        assert(strstr(hist, "\n\n") == NULL);
    }
    printf("PASSED\n");
}

static void test_recalled_line_is_clamped_to_rx(void)
{
    printf("Test: a recalled entry longer than rx is truncated... ");
    reset();
    feed("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n");  /* 30 chars, rx holds 31 */
    assert(strcmp(hist, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n") == 0);
    up();
    assert(ctx.rx_pos == 30u);
    printf("PASSED\n");
}

int main(void)
{
    printf("=== test_history ===\n");
    test_recording();
    test_recording_keeps_arguments();
    test_recording_skips_empty_lines();
    test_up_recalls();
    test_up_stops_at_oldest();
    test_up_redraws_the_line();
    test_down_restores_saved_input();
    test_down_walks_forward();
    test_down_at_zero_is_a_noop();
    test_saved_input_is_truncated();
    test_no_history_buffer();
    test_empty_history();
    test_eviction_drops_whole_entries();
    test_oversize_line_is_skipped();
    test_duplicates_are_kept();
    test_submit_resets_the_cursor();
    test_history_is_not_terminated_by_partial_entry();
    test_recalled_line_is_clamped_to_rx();
    printf("=== test_history: all PASSED ===\n");
    return 0;
}
