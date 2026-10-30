/* ============================================================================
 * elib_cli_core.c --- elib command line interface implementation (v2)
 * ============================================================================
 * The single source file; includes the umbrella header directly. The semantic
 * contract lives in the header banner of elib_cli.h:
 *   - one byte in, zero or more print callbacks out;
 *   - ANSI escape sequences are consumed whole, never injected into the line;
 *   - history, completion and echo all work on the caller-provided buffers only.
 * ==========================================================================*/
#include "elib_cli.h"
#include <string.h>

/* ------------------------------------------------------------------ constants */

/* ANSI decoder states (stored in ctx->escape_state) */
#define CLI_ESC_IDLE   0u              /* Not inside an escape sequence */
#define CLI_ESC_INTRO  1u              /* Got ESC, next byte selects the form */
#define CLI_ESC_BODY   2u              /* Got "ESC ["/"ESC O", collecting bytes */

/* Built-in command. It is a read-only descriptor, not a mutable global: every
 * context shares it, no slot of the caller's command table is consumed, and it
 * keeps working after several contexts have been initialized and deinitialized. */
#define CLI_HELP_NAME  "help"
#define CLI_HELP_TEXT  "Show available commands"

/* ------------------------------------------------------------------ output */

static void cli_print_raw(elib_cli_ctx_t *ctx, const char *s)
{
    ctx->cfg.print(ctx->cfg.user_data, "%s", s);
}

static void cli_print_char(elib_cli_ctx_t *ctx, char ch)
{
    char s[2];

    s[0] = ch;
    s[1] = '\0';
    cli_print_raw(ctx, s);
}

/* Emit the configured line terminator. The switch has a default so that an
 * impossible value can never leave the cursor mid-line; init rejects anything
 * outside the enum, so the default is unreachable by construction. */
static void cli_newline(elib_cli_ctx_t *ctx)
{
    switch (ctx->cfg.newline) {
    case ELIB_CLI_NL_LF:
        cli_print_raw(ctx, "\n");
        break;
    case ELIB_CLI_NL_CR:
        cli_print_raw(ctx, "\r");
        break;
    case ELIB_CLI_NL_CRLF:
        cli_print_raw(ctx, "\r\n");
        break;
    default:
        cli_print_raw(ctx, "\r\n");
        break;
    }
}

static void cli_prompt(elib_cli_ctx_t *ctx)
{
    if (ctx->cfg.prompt != NULL) {
        cli_print_raw(ctx, ctx->cfg.prompt);
    }
}

static int cli_echo_on(const elib_cli_ctx_t *ctx)
{
    return (ctx->cfg.flags & ELIB_CLI_FLAG_ECHO) != 0u;
}

static int cli_newline_valid(elib_cli_nl_t newline)
{
    switch (newline) {
    case ELIB_CLI_NL_LF:
    case ELIB_CLI_NL_CR:
    case ELIB_CLI_NL_CRLF:
        return 1;
    default:
        return 0;
    }
}

/* ------------------------------------------------------------------ history */

static uint32_t history_count(const elib_cli_ctx_t *ctx)
{
    uint32_t count = 0u;
    uint32_t i;

    if (ctx->cfg.history_buf == NULL ||
        ctx->cfg.history_buf_size < ELIB_CLI_MIN_HIST_BUF_SIZE) {
        return 0u;
    }
    for (i = 0u; ctx->cfg.history_buf[i] != '\0'; i++) {
        if (ctx->cfg.history_buf[i] == '\n') {
            count++;
        }
    }
    return count;
}

/* Start of the n-th command counted from the most recent one (0 = most recent).
 * Every entry is stored as "<line>\n", so the entry start is the byte after the
 * previous '\n'. Returns NULL when there is no such entry. */
static const char *history_find_nth(const elib_cli_ctx_t *ctx, uint32_t n)
{
    const char *buf = ctx->cfg.history_buf;
    uint32_t len;
    uint32_t total = 0u;
    uint32_t target;
    uint32_t index = 0u;
    uint32_t start = 0u;
    uint32_t i;

    if (buf == NULL) {
        return NULL;
    }
    len = (uint32_t)strlen(buf);
    if (len == 0u) {
        return NULL;
    }
    for (i = 0u; i < len; i++) {
        if (buf[i] == '\n') {
            total++;
        }
    }
    if (n >= total) {
        return NULL;
    }
    target = total - 1u - n;

    for (i = 0u; i < len; i++) {
        if (buf[i] == '\n') {
            if (index == target) {
                return &buf[start];
            }
            index++;
            start = i + 1u;
        }
    }
    return NULL;
}

/* Append one line. Old entries are dropped whole until the new one fits, so the
 * buffer never holds a half written command. A line that cannot fit even into an
 * empty buffer is skipped instead of destroying the history. */
static void history_append(elib_cli_ctx_t *ctx, const char *line)
{
    char *buf = ctx->cfg.history_buf;
    uint32_t size = ctx->cfg.history_buf_size;
    uint32_t line_len;
    uint32_t need;
    uint32_t used;

    if (buf == NULL || size < ELIB_CLI_MIN_HIST_BUF_SIZE) {
        return;
    }
    line_len = (uint32_t)strlen(line);
    if (line_len == 0u) {
        return;
    }
    need = line_len + 2u;              /* '\n' plus the terminating NUL */
    if (need > size) {
        return;
    }

    used = (uint32_t)strlen(buf);
    while (used + need > size) {
        char *nl = strchr(buf, '\n');

        if (nl == NULL) {
            /* Only an unterminated remainder is left: drop it all */
            buf[0] = '\0';
            used = 0u;
            break;
        }
        uint32_t drop = (uint32_t)(nl - buf) + 1u;

        /* Move the remaining bytes including the terminating NUL */
        memmove(buf, nl + 1, (size_t)(used - drop) + 1u);
        used -= drop;
    }

    memcpy(buf + used, line, line_len);
    buf[used + line_len] = '\n';
    buf[used + line_len + 1u] = '\0';
}

/* Copy the n-th entry into the receive buffer and update rx_pos. Returns 1 when
 * the entry exists (an empty entry is valid and yields rx_pos == 0), 0 when it
 * does not. */
static int history_copy_to_rx(elib_cli_ctx_t *ctx, uint32_t n)
{
    const char *entry = history_find_nth(ctx, n);
    const char *nl;
    uint32_t len;
    uint32_t cap;

    if (entry == NULL) {
        return 0;
    }
    nl = strchr(entry, '\n');
    len = (nl != NULL) ? (uint32_t)(nl - entry) : (uint32_t)strlen(entry);

    cap = ctx->cfg.rx_buf_size - 1u;
    if (len > cap) {
        len = cap;
    }
    if (len > 0u) {
        memcpy(ctx->cfg.rx_buf, entry, len);
    }
    ctx->cfg.rx_buf[len] = '\0';
    ctx->rx_pos = len;

    return 1;
}

/* ------------------------------------------------------------------ editing */

/* Erase the echoed line: one backspace-space-backspace per visible character */
static void cli_clear_line(elib_cli_ctx_t *ctx)
{
    uint32_t i;

    if (!cli_echo_on(ctx)) {
        return;
    }
    for (i = 0u; i < ctx->rx_pos; i++) {
        cli_print_raw(ctx, "\b \b");
    }
}

/* ------------------------------------------------------------------ history browsing */

static void cli_history_up(elib_cli_ctx_t *ctx)
{
    uint32_t total = history_count(ctx);
    uint32_t next;

    if (total == 0u) {
        return;
    }
    if (ctx->hist_offset < 0) {
        ctx->hist_offset = 0;
    }
    next = (uint32_t)ctx->hist_offset + 1u;
    if (next > total) {
        return;
    }

    if (ctx->hist_offset == 0) {
        /* Keep the line being typed so that Down can bring it back. The stored
         * length is the CLAMPED one, otherwise Down would copy more bytes than
         * the buffer holds. */
        uint32_t copy = ctx->rx_pos;

        if (ctx->cfg.saved_input != NULL &&
            ctx->cfg.saved_buf_size >= ELIB_CLI_MIN_SAVED_BUF_SIZE) {
            uint32_t cap = ctx->cfg.saved_buf_size - 1u;

            if (copy > cap) {
                copy = cap;
            }
            if (copy > 0u) {
                memcpy(ctx->cfg.saved_input, ctx->cfg.rx_buf, copy);
            }
            ctx->cfg.saved_input[copy] = '\0';
            ctx->saved_len = copy;
        } else {
            ctx->saved_len = 0u;
        }
    }

    cli_clear_line(ctx);

    if (history_copy_to_rx(ctx, next - 1u) != 0 && cli_echo_on(ctx)) {
        cli_print_raw(ctx, ctx->cfg.rx_buf);
    }
    ctx->hist_offset = (int32_t)next;
}

static void cli_history_down(elib_cli_ctx_t *ctx)
{
    int32_t next;

    if (ctx->hist_offset <= 0) {
        return;
    }
    next = ctx->hist_offset - 1;

    cli_clear_line(ctx);

    if (next == 0) {
        /* Restore the line that was being typed before the first Up */
        uint32_t len = ctx->saved_len;

        if (len >= ctx->cfg.rx_buf_size) {
            len = ctx->cfg.rx_buf_size - 1u;
        }
        if (len > 0u && ctx->cfg.saved_input != NULL) {
            memcpy(ctx->cfg.rx_buf, ctx->cfg.saved_input, len);
        }
        ctx->cfg.rx_buf[len] = '\0';
        ctx->rx_pos = len;
        if (len > 0u && cli_echo_on(ctx)) {
            cli_print_raw(ctx, ctx->cfg.rx_buf);
        }
    } else if (history_copy_to_rx(ctx, (uint32_t)next - 1u) != 0 &&
               cli_echo_on(ctx)) {
        cli_print_raw(ctx, ctx->cfg.rx_buf);
    }

    ctx->hist_offset = next;
}

/* ------------------------------------------------------------------ completion */

static int cli_match(const char *name, const char *prefix, uint32_t prefix_len)
{
    return strncmp(name, prefix, (size_t)prefix_len) == 0;
}

/* Has the user registered a command named "help"? Then the built-in listing is
 * shadowed and must not be offered a second time by completion or by itself. */
static int cli_help_overridden(const elib_cli_ctx_t *ctx)
{
    uint32_t i;

    for (i = 0u; i < ctx->cmd_count; i++) {
        if (strcmp(ctx->cfg.cmd_table[i].name, CLI_HELP_NAME) == 0) {
            return 1;
        }
    }
    return 0;
}

static void cli_tab_complete(elib_cli_ctx_t *ctx)
{
    const char *rx = ctx->cfg.rx_buf;
    uint32_t pos = ctx->rx_pos;
    uint32_t prefix_len;
    uint32_t matches = 0u;
    uint32_t single = 0u;
    uint32_t builtin_matches = 0u;
    int has_help;
    uint32_t i;

    if (pos == 0u) {
        return;
    }

    /* Completion only applies while the command name itself is being typed.
     * Once a space has been entered the cursor is in the arguments and TAB has
     * nothing to offer, so the line is left untouched. */
    prefix_len = pos;
    for (i = 0u; i < pos; i++) {
        if (rx[i] == ' ') {
            return;
        }
    }
    if (prefix_len == 0u) {
        return;
    }

    has_help = cli_help_overridden(ctx) ? 0 : 1;

    for (i = 0u; i < ctx->cmd_count; i++) {
        if (cli_match(ctx->cfg.cmd_table[i].name, rx, prefix_len)) {
            matches++;
            single = i;
        }
    }
    if (has_help && cli_match(CLI_HELP_NAME, rx, prefix_len)) {
        builtin_matches = 1u;
        matches++;
    }

    if (matches == 0u) {
        return;
    }

    if (matches == 1u) {
        const char *name = (builtin_matches != 0u)
                         ? CLI_HELP_NAME
                         : ctx->cfg.cmd_table[single].name;
        uint32_t name_len = (uint32_t)strlen(name);

        /* The completed name plus its trailing space must fit with the NUL */
        if (name_len + 2u > ctx->cfg.rx_buf_size) {
            return;
        }
        if (cli_echo_on(ctx)) {
            uint32_t k;

            for (k = 0u; k < pos; k++) {
                cli_print_raw(ctx, "\b \b");
            }
        }
        memcpy(ctx->cfg.rx_buf, name, name_len);
        ctx->cfg.rx_buf[name_len] = ' ';
        ctx->cfg.rx_buf[name_len + 1u] = '\0';
        ctx->rx_pos = name_len + 1u;
        if (cli_echo_on(ctx)) {
            cli_print_raw(ctx, ctx->cfg.rx_buf);
        }
        return;
    }

    /* Several matches: finish the input line, list them, redraw the prompt */
    cli_newline(ctx);
    for (i = 0u; i < ctx->cmd_count; i++) {
        if (cli_match(ctx->cfg.cmd_table[i].name, rx, prefix_len)) {
            cli_print_raw(ctx, ctx->cfg.cmd_table[i].name);
            cli_newline(ctx);
        }
    }
    if (builtin_matches != 0u) {
        cli_print_raw(ctx, CLI_HELP_NAME);
        cli_newline(ctx);
    }
    cli_prompt(ctx);
    if (cli_echo_on(ctx)) {
        cli_print_raw(ctx, rx);
    }
}

/* ------------------------------------------------------------------ command execution */

static void cli_print_entry(elib_cli_ctx_t *ctx, const char *name,
                            const char *help)
{
    if (help != NULL && help[0] != '\0') {
        ctx->cfg.print(ctx->cfg.user_data, "%-12s %s", name, help);
    } else {
        cli_print_raw(ctx, name);
    }
}

/* Built-in "help": list the user table, then the built-in itself. Lines are
 * separated here and the last one is terminated by the caller, so the result is
 * a clean block without a trailing blank line. */
static void cli_builtin_help(elib_cli_ctx_t *ctx)
{
    uint32_t i;
    uint32_t shown = 0u;

    for (i = 0u; i < ctx->cmd_count; i++) {
        if (shown > 0u) {
            cli_newline(ctx);
        }
        cli_print_entry(ctx, ctx->cfg.cmd_table[i].name,
                        ctx->cfg.cmd_table[i].help);
        shown++;
    }
    if (shown > 0u) {
        cli_newline(ctx);
    }
    cli_print_entry(ctx, CLI_HELP_NAME, CLI_HELP_TEXT);
}

/* Submit the current line. The buffer is consumed destructively: the command
 * name is terminated where its first space was, which is why the history entry
 * is appended before that happens. */
static elib_cli_err_t cli_execute(elib_cli_ctx_t *ctx)
{
    char *rx = ctx->cfg.rx_buf;
    char *cmd_start;
    char *cmd_end;
    char *args;
    elib_cli_err_t result = ELIB_CLI_ERR_CMD_NOT_FOUND;
    uint32_t i;

    rx[ctx->rx_pos] = '\0';

    cmd_start = rx;
    while (*cmd_start == ' ') {
        cmd_start++;
    }

    if (*cmd_start == '\0') {
        /* Empty line: just move to a fresh prompt */
        cli_newline(ctx);
        cli_prompt(ctx);
        ctx->rx_pos = 0u;
        rx[0] = '\0';
        ctx->hist_offset = 0;
        return ELIB_CLI_OK;
    }

    /* The whole line, arguments included, is what history must remember */
    history_append(ctx, cmd_start);

    cmd_end = cmd_start;
    while (*cmd_end != '\0' && *cmd_end != ' ') {
        cmd_end++;
    }
    args = cmd_end;
    if (*args == ' ') {
        *args = '\0';
        args++;
        while (*args == ' ') {
            args++;
        }
    }

    for (i = 0u; i < ctx->cmd_count; i++) {
        if (strcmp(ctx->cfg.cmd_table[i].name, cmd_start) == 0) {
            elib_cli_cmd_fn callback = ctx->cfg.cmd_table[i].callback;

            if (callback != NULL) {
                callback(ctx->cfg.user_data, args);
            }
            result = ELIB_CLI_OK;
            break;
        }
    }

    if (result != ELIB_CLI_OK) {
        if (strcmp(cmd_start, CLI_HELP_NAME) == 0) {
            cli_builtin_help(ctx);
            result = ELIB_CLI_OK;
        } else {
            ctx->cfg.print(ctx->cfg.user_data, "unknown command: %s", cmd_start);
        }
    }

    cli_newline(ctx);
    cli_prompt(ctx);
    ctx->rx_pos = 0u;
    rx[0] = '\0';
    ctx->hist_offset = 0;

    return result;
}

/* ------------------------------------------------------------------ lifecycle */

elib_cli_err_t elib_cli_init(elib_cli_ctx_t *ctx, const elib_cli_cfg_t *cfg)
{
    if (ctx == NULL || cfg == NULL) {
        return ELIB_CLI_ERR_INVALID_PARAM;
    }
    if (cfg->print == NULL) {
        return ELIB_CLI_ERR_INVALID_PARAM;
    }
    if (cfg->rx_buf == NULL || cfg->rx_buf_size < ELIB_CLI_MIN_RX_BUF_SIZE) {
        return ELIB_CLI_ERR_INVALID_PARAM;
    }
    if (cfg->cmd_table == NULL || cfg->cmd_table_size == 0u) {
        return ELIB_CLI_ERR_INVALID_PARAM;
    }
    /* An optional buffer is either fully specified or absent */
    if ((cfg->history_buf == NULL) != (cfg->history_buf_size == 0u)) {
        return ELIB_CLI_ERR_INVALID_PARAM;
    }
    if (cfg->history_buf != NULL &&
        cfg->history_buf_size < ELIB_CLI_MIN_HIST_BUF_SIZE) {
        return ELIB_CLI_ERR_INVALID_PARAM;
    }
    if ((cfg->saved_input == NULL) != (cfg->saved_buf_size == 0u)) {
        return ELIB_CLI_ERR_INVALID_PARAM;
    }
    if (cfg->saved_input != NULL &&
        cfg->saved_buf_size < ELIB_CLI_MIN_SAVED_BUF_SIZE) {
        return ELIB_CLI_ERR_INVALID_PARAM;
    }
    if (!cli_newline_valid(cfg->newline)) {
        return ELIB_CLI_ERR_INVALID_PARAM;
    }
    /* Unknown flag bits would silently change the echo contract */
    if ((cfg->flags & ~ELIB_CLI_FLAG_ECHO) != 0u) {
        return ELIB_CLI_ERR_INVALID_PARAM;
    }

    /* Unconditional re-initialization: the caller's historical state is irrelevant */
    memset(ctx, 0, sizeof(*ctx));
    ctx->cfg = *cfg;
    ctx->initialized = 1u;

    /* The caller's buffers start empty so that no stale byte is ever parsed */
    ctx->cfg.rx_buf[0] = '\0';
    if (ctx->cfg.history_buf != NULL) {
        ctx->cfg.history_buf[0] = '\0';
    }
    if (ctx->cfg.saved_input != NULL) {
        ctx->cfg.saved_input[0] = '\0';
    }

    return ELIB_CLI_OK;
}

void elib_cli_deinit(elib_cli_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }
    /* The configuration stays readable but every other API rejects from now on */
    ctx->rx_pos = 0u;
    ctx->saved_len = 0u;
    ctx->hist_offset = 0;
    ctx->escape_state = CLI_ESC_IDLE;
    ctx->crlf_skip_lf = 0u;
    ctx->initialized = 0u;
}

/* ------------------------------------------------------------------ commands */

elib_cli_err_t elib_cli_register(elib_cli_ctx_t *ctx, const char *name,
                                 const char *help, elib_cli_cmd_fn callback)
{
    uint32_t i;

    if (ctx == NULL || name == NULL || name[0] == '\0' || callback == NULL) {
        return ELIB_CLI_ERR_INVALID_PARAM;
    }
    if (!ctx->initialized) {
        return ELIB_CLI_ERR_NOT_INITIALIZED;
    }

    /* Replacing keeps the slot: that is how "help" and any other command is
     * overridden without growing the table */
    for (i = 0u; i < ctx->cmd_count; i++) {
        if (strcmp(ctx->cfg.cmd_table[i].name, name) == 0) {
            ctx->cfg.cmd_table[i].help = help;
            ctx->cfg.cmd_table[i].callback = callback;
            return ELIB_CLI_OK;
        }
    }

    if (ctx->cmd_count >= ctx->cfg.cmd_table_size) {
        return ELIB_CLI_ERR_CMD_TABLE_FULL;
    }

    ctx->cfg.cmd_table[ctx->cmd_count].name = name;
    ctx->cfg.cmd_table[ctx->cmd_count].help = help;
    ctx->cfg.cmd_table[ctx->cmd_count].callback = callback;
    ctx->cmd_count++;

    return ELIB_CLI_OK;
}

/* ------------------------------------------------------------------ input */

/* Interpret one byte once the line-terminator logic has had its say. Escape
 * sequences, editing keys and printable characters all live here, so the
 * tolerant CRLF path can re-dispatch the byte that followed a lone '\r'. */
static elib_cli_err_t cli_feed_byte(elib_cli_ctx_t *ctx, char ch)
{
    /* ANSI escape sequences are consumed whole: parameter bytes (0x30..0x3F)
     * and intermediate bytes (0x20..0x2F) keep the decoder in the body state and
     * only the final byte (0x40..0x7E) ends it. A parameterised sequence
     * therefore cannot leak its payload into the edited line. */
    if (ctx->escape_state == CLI_ESC_INTRO) {
        if (ch == '[' || ch == 'O') {
            ctx->escape_state = CLI_ESC_BODY;
        } else {
            ctx->escape_state = CLI_ESC_IDLE;    /* Single byte sequence */
        }
        return ELIB_CLI_OK;
    }
    if (ctx->escape_state == CLI_ESC_BODY) {
        unsigned char byte = (unsigned char)ch;

        if ((byte >= 0x30u && byte <= 0x3Fu) || (byte >= 0x20u && byte <= 0x2Fu)) {
            return ELIB_CLI_OK;                  /* Still inside the body */
        }
        ctx->escape_state = CLI_ESC_IDLE;
        if (byte == 'A') {
            cli_history_up(ctx);
        } else if (byte == 'B') {
            cli_history_down(ctx);
        }
        return ELIB_CLI_OK;
    }

    /* Backspace (0x08 and the DEL of a real terminal) */
    if (ch == 0x08 || ch == 0x7F) {
        if (ctx->rx_pos > 0u) {
            ctx->rx_pos--;
            ctx->cfg.rx_buf[ctx->rx_pos] = '\0';
            if (cli_echo_on(ctx)) {
                cli_print_raw(ctx, "\b \b");
            }
        }
        return ELIB_CLI_OK;
    }

    /* Start of an escape sequence */
    if (ch == 0x1B) {
        ctx->escape_state = CLI_ESC_INTRO;
        return ELIB_CLI_OK;
    }

    /* Tab completion */
    if (ch == '\t') {
        cli_tab_complete(ctx);
        return ELIB_CLI_OK;
    }

    /* Printable character */
    if (ch >= 0x20 && ch <= 0x7E) {
        if (ctx->rx_pos >= ctx->cfg.rx_buf_size - 1u) {
            return ELIB_CLI_ERR_RX_OVERFLOW;
        }
        ctx->cfg.rx_buf[ctx->rx_pos] = ch;
        ctx->rx_pos++;
        ctx->cfg.rx_buf[ctx->rx_pos] = '\0';
        if (cli_echo_on(ctx)) {
            cli_print_char(ctx, ch);
        }
    }

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

    /* An escape sequence in progress owns the next byte. ESC bodies routinely
     * contain 0x0D/0x0A, so the line-terminator logic must not see them. */
    if (ctx->escape_state != CLI_ESC_IDLE) {
        return cli_feed_byte(ctx, ch);
    }

    /* In CRLF mode a '\r' submits at once (tolerant mode) and the '\n' that may
     * follow it is swallowed. Deferring the submit until the next byte instead
     * would make a lone '\r' look like a dead key at the end of a burst. */
    if (ctx->cfg.newline == ELIB_CLI_NL_CRLF) {
        if (ctx->crlf_skip_lf != 0u) {
            ctx->crlf_skip_lf = 0u;
            if (ch == '\n') {
                return ELIB_CLI_OK;
            }
            /* Not the partner byte: interpret it normally below */
        }
        if (ch == '\r') {
            ctx->crlf_skip_lf = 1u;
            return cli_execute(ctx);
        }
    } else if ((ctx->cfg.newline == ELIB_CLI_NL_LF && ch == '\n') ||
               (ctx->cfg.newline == ELIB_CLI_NL_CR && ch == '\r')) {
        return cli_execute(ctx);
    }

    return cli_feed_byte(ctx, ch);
}

/* ------------------------------------------------------------------ argument helpers */

uint32_t elib_cli_argc(const char *args)
{
    uint32_t count = 0u;

    if (args == NULL) {
        return 0u;
    }
    while (*args != '\0') {
        while (*args == ' ') {
            args++;
        }
        if (*args == '\0') {
            break;
        }
        count++;
        while (*args != '\0' && *args != ' ') {
            args++;
        }
    }

    return count;
}

const char *elib_cli_argv(const char *args, uint32_t index, uint32_t *len)
{
    uint32_t current = 0u;

    if (len != NULL) {
        *len = 0u;
    }
    if (args == NULL) {
        return NULL;
    }

    while (*args != '\0') {
        while (*args == ' ') {
            args++;
        }
        if (*args == '\0') {
            break;
        }
        if (current == index) {
            const char *start = args;

            while (*args != '\0' && *args != ' ') {
                args++;
            }
            if (len != NULL) {
                *len = (uint32_t)(args - start);
            }
            return start;
        }
        current++;
        while (*args != '\0' && *args != ' ') {
            args++;
        }
    }

    return NULL;
}
