/* elib_cli_core.c - CLI Core Implementation */
#include "elib_cli_core.h"
#include <string.h>

/* --- Internal helpers --- */

static void cli_print_raw(elib_cli_ctx_t *ctx, const char *s)
{
    if (ctx->cfg->print) {
        ctx->cfg->print("%s", s);
    }
}

static void cli_newline(elib_cli_ctx_t *ctx)
{
    switch (ctx->cfg->newline) {
    case ELIB_CLI_NL_LF:   cli_print_raw(ctx, "\n"); break;
    case ELIB_CLI_NL_CR:   cli_print_raw(ctx, "\r"); break;
    case ELIB_CLI_NL_CRLF: cli_print_raw(ctx, "\r\n"); break;
    }
}

static void cli_prompt(elib_cli_ctx_t *ctx)
{
    if (ctx->cfg->prompt) {
        cli_print_raw(ctx, ctx->cfg->prompt);
    }
}

/* --- History ring buffer --- */

/* History is stored as \n-separated strings: "cmd1\ncmd2\ncmd3\n"
   We track content length and always append at the end. */

static void history_clear(elib_cli_ctx_t *ctx)
{
    ctx->cfg->history_buf[0] = '\0';
}

/* Find the start of the N-th command from the end (0 = most recent).
   Returns pointer into history_buf, or NULL if not enough commands.
   History format: "cmd1\ncmd2\ncmd3\n" — each command ends with \n.
   No temp array: first pass counts \n's, second pass locates target. */
static char *history_find_nth(elib_cli_ctx_t *ctx, int n)
{
    char *buf = ctx->cfg->history_buf;
    int len = (int)strlen(buf);

    if (len == 0 || n < 0) {
        return NULL;
    }

    /* Count total \n's (= total commands) */
    int total = 0;
    for (int i = 0; i < len; i++) {
        if (buf[i] == '\n') total++;
    }
    if (n >= total) {
        return NULL;
    }

    /* Convert n-th from end to index from beginning */
    int target = total - 1 - n;
    int cmd_idx = 0;
    int start = 0;

    for (int i = 0; i < len; i++) {
        if (buf[i] == '\n') {
            if (cmd_idx == target) {
                return &buf[start];
            }
            cmd_idx++;
            start = i + 1;
        }
    }

    return NULL;
}

/* Get the number of commands in history */
static int history_count(elib_cli_ctx_t *ctx)
{
    if (ctx->cfg->history_buf == NULL || ctx->cfg->history_buf_size == 0) {
        return 0;
    }
    int count = 0;
    for (int i = 0; ctx->cfg->history_buf[i]; i++) {
        if (ctx->cfg->history_buf[i] == '\n') {
            count++;
        }
    }
    return count;
}

/* Make room in history for a command of cmd_len bytes (including \n).
   Removes oldest commands from the head until enough space. */
static void history_make_room(elib_cli_ctx_t *ctx, uint16_t cmd_len)
{
    char *buf = ctx->cfg->history_buf;
    uint16_t size = ctx->cfg->history_buf_size;
    uint16_t used = (uint16_t)strlen(buf);
    uint16_t needed = used + cmd_len;

    while (needed > size) {
        /* Find first \n - everything before and including it is removed */
        char *nl = strchr(buf, '\n');
        if (nl == NULL) {
            /* No complete command to remove, clear everything */
            history_clear(ctx);
            return;
        }
        uint16_t remove = (uint16_t)(nl - buf) + 1;
        uint16_t remaining = used - remove;
        memmove(buf, nl + 1, remaining);
        buf[remaining] = '\0';
        used = remaining;
        needed = used + cmd_len;
    }
}

/* Append a command to history */
static void history_append(elib_cli_ctx_t *ctx, const char *cmd)
{
    if (ctx->cfg->history_buf == NULL || ctx->cfg->history_buf_size == 0) {
        return;
    }

    uint16_t cmd_len = (uint16_t)strlen(cmd);
    uint16_t total = cmd_len + 1; /* +1 for \n */

    if (total >= ctx->cfg->history_buf_size) {
        /* Command too long for buffer, clear and store nothing */
        history_clear(ctx);
        return;
    }

    history_make_room(ctx, total);

    char *buf = ctx->cfg->history_buf;
    uint16_t used = (uint16_t)strlen(buf);
    memcpy(buf + used, cmd, cmd_len);
    buf[used + cmd_len] = '\n';
    buf[used + cmd_len + 1] = '\0';
}

/* Copy n-th history command (from end) directly into rx_buf.
   Returns copied length, or 0 if not found. */
static uint16_t history_copy_to_rx(elib_cli_ctx_t *ctx, int n)
{
    char *cmd = history_find_nth(ctx, n);
    if (cmd == NULL) {
        return 0;
    }

    char *nl = strchr(cmd, '\n');
    uint16_t len = nl ? (uint16_t)(nl - cmd) : (uint16_t)strlen(cmd);

    if (len >= ctx->cfg->rx_buf_size) {
        len = ctx->cfg->rx_buf_size - 1;
    }
    memcpy(ctx->cfg->rx_buf, cmd, len);
    ctx->cfg->rx_buf[len] = '\0';
    return len;
}

/* --- Tab completion --- */

static void cli_tab_complete(elib_cli_ctx_t *ctx)
{
    char *rx = ctx->cfg->rx_buf;
    uint16_t pos = ctx->rx_pos;

    /* Find prefix: everything before first space */
    uint16_t prefix_len = 0;
    for (uint16_t i = 0; i < pos; i++) {
        if (rx[i] == ' ') {
            prefix_len = i;
            break;
        }
    }
    if (prefix_len == 0 && pos > 0) {
        prefix_len = pos;
    }
    if (prefix_len == 0) {
        return;
    }

    /* Count matches and find last match */
    int match_count = 0;
    int last_match = -1;
    int single_match = -1;
    for (uint8_t i = 0; i < ctx->cmd_count; i++) {
        const char *name = ctx->cfg->cmd_table[i].name;
        if (strncmp(name, rx, prefix_len) == 0) {
            match_count++;
            last_match = i;
            single_match = (match_count == 1) ? i : -1;
        }
    }

    if (match_count == 0) {
        return;
    }

    if (match_count == 1) {
        /* Auto-complete */
        const char *name = ctx->cfg->cmd_table[single_match].name;
        uint16_t name_len = (uint16_t)strlen(name);
        if (name_len < ctx->cfg->rx_buf_size - 2) {
            /* Erase current input and rewrite */
            if (ctx->cfg->echo) {
                for (uint16_t i = 0; i < pos; i++) {
                    cli_print_raw(ctx, "\b \b");
                }
            }
            memcpy(rx, name, name_len);
            rx[name_len] = ' ';
            rx[name_len + 1] = '\0';
            ctx->rx_pos = name_len + 1;
            if (ctx->cfg->echo) {
                cli_print_raw(ctx, rx);
            }
        }
        return;
    }

    /* Multiple matches: list them */
    cli_newline(ctx);
    for (uint8_t i = 0; i < ctx->cmd_count; i++) {
        const char *name = ctx->cfg->cmd_table[i].name;
        if (strncmp(name, rx, prefix_len) == 0) {
            ctx->cfg->print("%s\n", name);
        }
    }
    cli_prompt(ctx);
    if (ctx->cfg->echo) {
        cli_print_raw(ctx, rx);
    }
}

/* --- History browsing --- */

static void cli_clear_line(elib_cli_ctx_t *ctx)
{
    if (!ctx->cfg->echo) {
        return;
    }
    for (uint16_t i = 0; i < ctx->rx_pos; i++) {
        cli_print_raw(ctx, "\b \b");
    }
}

static void cli_history_up(elib_cli_ctx_t *ctx)
{
    int total = history_count(ctx);
    if (total == 0) {
        return;
    }

    int new_offset = ctx->hist_offset + 1;
    if (new_offset > total) {
        return;
    }

    /* Save current input on first up press */
    if (ctx->hist_offset == 0) {
        ctx->saved_len = ctx->rx_pos;
        if (ctx->cfg->saved_input && ctx->cfg->saved_buf_size > 0) {
            uint16_t copy = ctx->rx_pos;
            if (copy >= ctx->cfg->saved_buf_size) {
                copy = ctx->cfg->saved_buf_size - 1;
            }
            memcpy(ctx->cfg->saved_input, ctx->cfg->rx_buf, copy);
            ctx->cfg->saved_input[copy] = '\0';
        }
    }

    cli_clear_line(ctx);

    /* Copy directly from history_buf to rx_buf */
    uint16_t len = history_copy_to_rx(ctx, new_offset - 1);
    if (len > 0) {
        ctx->rx_pos = len;
        if (ctx->cfg->echo) {
            cli_print_raw(ctx, ctx->cfg->rx_buf);
        }
    }

    ctx->hist_offset = new_offset;
}

static void cli_history_down(elib_cli_ctx_t *ctx)
{
    if (ctx->hist_offset <= 0) {
        return;
    }

    int new_offset = ctx->hist_offset - 1;
    cli_clear_line(ctx);

    if (new_offset == 0) {
        /* Restore saved input */
        if (ctx->cfg->saved_input && ctx->saved_len > 0) {
            uint16_t len = ctx->saved_len;
            if (len >= ctx->cfg->rx_buf_size) {
                len = ctx->cfg->rx_buf_size - 1;
            }
            memcpy(ctx->cfg->rx_buf, ctx->cfg->saved_input, len);
            ctx->cfg->rx_buf[len] = '\0';
            ctx->rx_pos = len;
            if (ctx->cfg->echo) {
                cli_print_raw(ctx, ctx->cfg->rx_buf);
            }
        } else {
            ctx->cfg->rx_buf[0] = '\0';
            ctx->rx_pos = 0;
        }
    } else {
        /* Copy directly from history_buf to rx_buf */
        uint16_t len = history_copy_to_rx(ctx, new_offset - 1);
        if (len > 0) {
            ctx->rx_pos = len;
            if (ctx->cfg->echo) {
                cli_print_raw(ctx, ctx->cfg->rx_buf);
            }
        }
    }

    ctx->hist_offset = new_offset;
}

/* --- Command execution --- */

/* Built-in help command callback */
static elib_cli_ctx_t *help_ctx_ref;

static void cli_builtin_help(const char *args)
{
    (void)args;
    elib_cli_ctx_t *ctx = help_ctx_ref;
    for (uint8_t i = 0; i < ctx->cmd_count; i++) {
        const char *name = ctx->cfg->cmd_table[i].name;
        const char *help = ctx->cfg->cmd_table[i].help;
        if (help && help[0]) {
            ctx->cfg->print("%-12s %s\n", name, help);
        } else {
            ctx->cfg->print("%s\n", name);
        }
    }
}

static void cli_execute(elib_cli_ctx_t *ctx)
{
    char *rx = ctx->cfg->rx_buf;
    rx[ctx->rx_pos] = '\0';

    /* Skip leading spaces */
    char *cmd_start = rx;
    while (*cmd_start == ' ') {
        cmd_start++;
    }

    if (*cmd_start == '\0') {
        /* Empty line */
        cli_newline(ctx);
        cli_prompt(ctx);
        ctx->rx_pos = 0;
        rx[0] = '\0';
        ctx->hist_offset = 0;
        return;
    }

    /* Find end of command name */
    char *cmd_end = cmd_start;
    while (*cmd_end != '\0' && *cmd_end != ' ') {
        cmd_end++;
    }

    /* Find args: skip spaces after command name */
    char *args = cmd_end;
    if (*args == ' ') {
        *args = '\0';
        args++;
        while (*args == ' ') {
            args++;
        }
    }
    /* If no args, args points to '\0' which is fine */

    /* Add to history before execution */
    history_append(ctx, cmd_start);

    /* Find command */
    for (uint8_t i = 0; i < ctx->cmd_count; i++) {
        if (strcmp(ctx->cfg->cmd_table[i].name, cmd_start) == 0) {
            ctx->cfg->cmd_table[i].callback(args);
            cli_newline(ctx);
            cli_prompt(ctx);
            ctx->rx_pos = 0;
            rx[0] = '\0';
            ctx->hist_offset = 0;
            return;
        }
    }

    /* Unknown command */
    ctx->cfg->print("unknown command: %s\n", cmd_start);
    cli_newline(ctx);
    cli_prompt(ctx);
    ctx->rx_pos = 0;
    rx[0] = '\0';
    ctx->hist_offset = 0;
}

/* --- Public API --- */

elib_cli_err_t elib_cli_init(elib_cli_ctx_t *ctx, const elib_cli_cfg_t *cfg)
{
    if (ctx == NULL || cfg == NULL) {
        return ELIB_CLI_ERR_INVALID_PARAM;
    }
    if (cfg->print == NULL || cfg->rx_buf == NULL || cfg->cmd_table == NULL) {
        return ELIB_CLI_ERR_INVALID_PARAM;
    }
    if (cfg->rx_buf_size == 0 || cfg->cmd_table_size == 0) {
        return ELIB_CLI_ERR_INVALID_PARAM;
    }

    memset(ctx, 0, sizeof(elib_cli_ctx_t));
    ctx->cfg = cfg;
    ctx->initialized = 1;

    cfg->rx_buf[0] = '\0';
    if (cfg->history_buf && cfg->history_buf_size > 0) {
        cfg->history_buf[0] = '\0';
    }

    /* Register built-in help command (can be overridden by user) */
    help_ctx_ref = ctx;
    elib_cli_register(ctx, "help", "Show available commands", cli_builtin_help);

    return ELIB_CLI_OK;
}

void elib_cli_deinit(elib_cli_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }
    ctx->initialized = 0;
}

elib_cli_err_t elib_cli_register(elib_cli_ctx_t *ctx, const char *name,
                                  const char *help, elib_cli_cmd_fn callback)
{
    if (ctx == NULL || name == NULL || callback == NULL) {
        return ELIB_CLI_ERR_INVALID_PARAM;
    }
    if (!ctx->initialized) {
        return ELIB_CLI_ERR_NOT_INITIALIZED;
    }
    if (ctx->cmd_count >= ctx->cfg->cmd_table_size) {
        return ELIB_CLI_ERR_CMD_TABLE_FULL;
    }

    /* Check for duplicate */
    for (uint8_t i = 0; i < ctx->cmd_count; i++) {
        if (strcmp(ctx->cfg->cmd_table[i].name, name) == 0) {
            /* Override existing */
            ctx->cfg->cmd_table[i].help = help;
            ctx->cfg->cmd_table[i].callback = callback;
            return ELIB_CLI_OK;
        }
    }

    /* Add new */
    ctx->cfg->cmd_table[ctx->cmd_count].name = name;
    ctx->cfg->cmd_table[ctx->cmd_count].help = help;
    ctx->cfg->cmd_table[ctx->cmd_count].callback = callback;
    ctx->cmd_count++;

    return ELIB_CLI_OK;
}

elib_cli_err_t elib_cli_feed_char(elib_cli_ctx_t *ctx, char ch)
{
    if (ctx == NULL) {
        return ELIB_CLI_ERR_INVALID_PARAM;
    }
    if (!ctx->initialized) {
        return ELIB_CLI_ERR_NOT_INITIALIZED;
    }

    /* ANSI escape sequence state machine */
    if (ctx->escape_state == 1) {
        if (ch == '[') {
            ctx->escape_state = 2;
        } else {
            ctx->escape_state = 0;
        }
        return ELIB_CLI_OK;
    }
    if (ctx->escape_state == 2) {
        ctx->escape_state = 0;
        if (ch == 'A') {
            cli_history_up(ctx);
            return ELIB_CLI_OK;
        }
        if (ch == 'B') {
            cli_history_down(ctx);
            return ELIB_CLI_OK;
        }
        return ELIB_CLI_OK;
    }

    /* Newline handling */
    if (ctx->cfg->newline == ELIB_CLI_NL_LF && ch == '\n') {
        cli_execute(ctx);
        return ELIB_CLI_OK;
    }
    if (ctx->cfg->newline == ELIB_CLI_NL_CR && ch == '\r') {
        cli_execute(ctx);
        return ELIB_CLI_OK;
    }
    if (ctx->cfg->newline == ELIB_CLI_NL_CRLF) {
        if (ch == '\r') {
            ctx->cr_pending = 1;
            return ELIB_CLI_OK;
        }
        if (ctx->cr_pending) {
            ctx->cr_pending = 0;
            if (ch == '\n') {
                cli_execute(ctx);
                return ELIB_CLI_OK;
            }
            /* \r followed by non-\n: treat \r as newline (tolerant) */
            cli_execute(ctx);
            /* fall through to process current character */
        }
    }

    /* Backspace */
    if (ch == 0x08 || ch == 0x7F) {
        if (ctx->rx_pos > 0) {
            ctx->rx_pos--;
            ctx->cfg->rx_buf[ctx->rx_pos] = '\0';
            if (ctx->cfg->echo) {
                cli_print_raw(ctx, "\b \b");
            }
        }
        return ELIB_CLI_OK;
    }

    /* ESC - start of ANSI sequence */
    if (ch == 0x1b) {
        ctx->escape_state = 1;
        return ELIB_CLI_OK;
    }

    /* Tab */
    if (ch == '\t') {
        cli_tab_complete(ctx);
        return ELIB_CLI_OK;
    }

    /* Regular printable character */
    if (ch >= 0x20 && ch <= 0x7E) {
        if (ctx->rx_pos < ctx->cfg->rx_buf_size - 1) {
            ctx->cfg->rx_buf[ctx->rx_pos] = ch;
            ctx->rx_pos++;
            ctx->cfg->rx_buf[ctx->rx_pos] = '\0';
            if (ctx->cfg->echo) {
                char s[2] = {ch, '\0'};
                cli_print_raw(ctx, s);
            }
        } else {
            return ELIB_CLI_ERR_RX_OVERFLOW;
        }
    }

    return ELIB_CLI_OK;
}

int elib_cli_argc(const char *args)
{
    if (args == NULL || *args == '\0') {
        return 0;
    }

    int count = 0;
    const char *p = args;
    while (*p) {
        while (*p == ' ') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        count++;
        while (*p && *p != ' ') {
            p++;
        }
    }
    return count;
}

const char *elib_cli_argv(const char *args, int index)
{
    if (args == NULL || index < 0) {
        return NULL;
    }

    int current = 0;
    const char *p = args;
    while (*p) {
        while (*p == ' ') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        if (current == index) {
            return p;
        }
        current++;
        while (*p && *p != ' ') {
            p++;
        }
    }
    return NULL;
}
