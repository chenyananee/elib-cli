/* ============================================================================
 * elib_cli.h --- elib command line interface (elib-cli) unified umbrella header
 * ============================================================================
 * The complete public API and public types of this library live in this single
 * header. All sub-headers were removed as of v2.
 * Version: ELIB_CLI_VERSION (currently 2.0.0)
 *
 * Game rules (elib family v2 common principles):
 *   1. C99; must compile warning-free with
 *      arm-gcc -std=c99 -Wall -Wextra -Werror -Wpedantic -Iinclude.
 *   2. Zero dynamic memory, zero globals: the context, the receive buffer, the
 *      history buffer, the saved-input buffer and the command table are all
 *      provided statically by the user. The library owns no state of its own and
 *      keeps no mutable file-scope variable.
 *   3. init memset-overwrites and re-initializes unconditionally; a ctx in any
 *      historical state is accepted; illegal configurations (NULL / an empty
 *      command table / a receive buffer below ELIB_CLI_MIN_RX_BUF_SIZE / a
 *      history buffer below 3 bytes / a saved-input buffer below 2 bytes / a
 *      half-specified optional buffer / an out-of-range newline / an unknown
 *      flag bit) are rejected.
 *   4. Sources include this umbrella header directly: #include "elib_cli.h",
 *      built with -Iinclude; relative-path cross-layer includes are forbidden.
 *   5. No build system ships with the library.
 *
 * Input model:
 *   The caller pulls bytes out of its own transport and hands them to
 *   elib_cli_feed_char() one at a time. The library never touches the transport
 *   itself except through the print callback. One byte is interpreted as:
 *
 *     0x20..0x7E   printable: appended to the line, echoed when echo is on
 *     0x08 / 0x7F  backspace: removes the last byte, echoes "\b \b"
 *     ESC          start of an ANSI escape sequence, see below
 *     TAB          command completion
 *     newline      submits the line, see "Line endings"
 *     anything  else is ignored (no control characters leak into the line)
 *
 *   ANSI escape sequences are consumed as whole units so that terminals which
 *   send parameterised variants cannot inject their payload into the line:
 *
 *     ESC [ <param 0x30..0x3F>* <intermediate 0x20..0x2F>* <final 0x40..0x7E>
 *     ESC O <final>                       (SS3, application cursor keys)
 *
 *   The final byte A moves to the previous history entry, B to the next one;
 *   every other sequence is swallowed. A stray ESC swallows the next byte.
 *
 * Line endings:
 *   ELIB_CLI_NL_LF submits on '\n', ELIB_CLI_NL_CR on '\r' and
 *   ELIB_CLI_NL_CRLF on "\r\n". CRLF mode is tolerant: a '\r' submits the line
 *   immediately and a '\n' that follows it is swallowed, so "\r\n" and a lone
 *   '\r' behave the same and the command never waits for the next byte. A lone
 *   '\n' is ignored. The same setting decides which terminator the library
 *   itself emits, so the terminal echo and the library output agree.
 *
 * Command model:
 *   A submitted line is split on single spaces: the first token is the command
 *   name, everything after it (leading spaces removed, inner spacing preserved)
 *   is passed to the callback as one string. Names are case sensitive.
 *   "help" is built in: it lists the command table plus itself. It does not
 *   occupy a slot of the caller's command table and it can be replaced by
 *   registering "help" like any other command, in which case the user callback
 *   runs instead of the built-in listing.
 *
 *   Every command callback receives the ctx user_data cookie, so a command needs
 *   no global of its own and several CLI contexts can coexist in one program.
 *   The callback runs from inside feed_char: it must not call feed_char(),
 *   register() or deinit() on the same context, and it must not block for long.
 *   The library terminates the line after the callback returns, so a callback
 *   ends its output without a trailing newline; printing the history, the "> "
 *   prompt and so on is the caller's business and must go through the same
 *   print callback to stay ordered.
 *
 * History model:
 *   The whole submitted line (command and arguments) is appended to the history
 *   buffer as "<line>\n". When the buffer is full the oldest entries are removed
 *   whole, so a partially written entry never appears. Consecutive identical
 *   lines are stored again; deduplication is left to the caller.
 *
 *   Up/Down walk entries counted from the most recent one. The line being typed
 *   is copied into the saved-input buffer on the first Up and restored when Down
 *   returns to offset zero. A history overrides the line even when it is shorter
 *   than what was typed.
 *
 * Completion model:
 *   TAB compares the first token of the line against the command table and the
 *   built-in help. A single match is completed and a space is appended; several
 *   matches are listed and the line is redrawn; no match changes nothing.
 *
 * Echo model:
 *   With ELIB_CLI_FLAG_ECHO the library mirrors what it consumes: each accepted
 *   character, "\b \b" per erased character, and the redrawn line after a
 *   history step or a completion. Without the flag the library prints no input
 *   echo at all and expects the terminal (or the caller) to do it.
 *
 * Concurrency model:
 *   feed_char() mutates the shared line state, so it is not re-entrant. Feed it
 *   from exactly one context: either the main loop or one ISR, never both at
 *   once, and never from inside a command callback or the print callback. The
 *   print callback is the only function the library calls back, and it may be
 *   called many times per input byte (once per echoed character).
 * ==========================================================================*/
#ifndef ELIB_CLI_H
#define ELIB_CLI_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------- version */
#define ELIB_CLI_VERSION        "2.0.0"
#define ELIB_CLI_VERSION_MAJOR  2
#define ELIB_CLI_VERSION_MINOR  0
#define ELIB_CLI_VERSION_PATCH  0

/* --------------------------------------------------------------- error codes */
typedef enum {
    ELIB_CLI_OK = 0,                   /* Success (also: nothing to do) */
    ELIB_CLI_ERR_INVALID_PARAM,        /* Illegal parameter or configuration */
    ELIB_CLI_ERR_NOT_INITIALIZED,      /* Context not initialized, or deinitialized */
    ELIB_CLI_ERR_CMD_TABLE_FULL,       /* No free slot left in the command table */
    ELIB_CLI_ERR_RX_OVERFLOW,          /* Line is full, the byte was dropped */
    ELIB_CLI_ERR_CMD_NOT_FOUND,        /* Submitted line matched no command */
} elib_cli_err_t;

/* --------------------------------------------------------------- line ending */
typedef enum {
    ELIB_CLI_NL_LF = 0,                /* '\n' */
    ELIB_CLI_NL_CR,                    /* '\r' */
    ELIB_CLI_NL_CRLF,                  /* "\r\n" (a lone '\r' is tolerated) */
} elib_cli_nl_t;

/* --------------------------------------------------------------- flags */
/* Mirror accepted input: characters, backspaces and redrawn lines. Without it
 * the library stays silent and the terminal (or the caller) owns the echo. */
#define ELIB_CLI_FLAG_ECHO         (1u << 0)

/* Smallest accepted receive buffer: one character plus its terminator */
#define ELIB_CLI_MIN_RX_BUF_SIZE   2u
/* Smallest accepted history buffer when one is supplied: "c\n" plus NUL */
#define ELIB_CLI_MIN_HIST_BUF_SIZE 3u
/* Smallest accepted saved-input buffer when one is supplied */
#define ELIB_CLI_MIN_SAVED_BUF_SIZE 2u

/* --------------------------------------------------------------- callbacks */
/* Transport. printf-style, user_data is the ctx cookie. Called many times per
 * input byte, so keep it non-blocking and copy the text when the transport is
 * asynchronous. */
typedef int (*elib_cli_print_fn)(void *user_data, const char *fmt, ...);

/* Command handler. args is the text after the command name, never NULL (it is
 * an empty string when the command has no arguments). The string points into
 * the receive buffer and is only valid for the duration of the call. */
typedef void (*elib_cli_cmd_fn)(void *user_data, const char *args);

/* Command descriptor (one slot of the caller-provided table) */
typedef struct {
    const char *name;                  /* Case sensitive, no spaces */
    const char *help;                  /* Optional one-line description, may be NULL */
    elib_cli_cmd_fn callback;
} elib_cli_cmd_t;

/* --------------------------------------------------------------- configuration */
/* Value struct: init copies every field, no pointer to it is kept. The buffers
 * and the table it points at must outlive the context. */
typedef struct {
    const char *prompt;                /* Optional prompt, may be NULL */
    elib_cli_nl_t newline;             /* Line terminator for input and output */
    elib_cli_print_fn print;           /* Mandatory transport */
    void *user_data;                   /* Opaque cookie passed to both callbacks */
    char *rx_buf;                      /* Receive buffer, at least 2 bytes */
    uint32_t rx_buf_size;
    char *history_buf;                 /* Optional, at least 3 bytes; NULL disables */
    uint32_t history_buf_size;
    elib_cli_cmd_t *cmd_table;         /* Command slots, at least one */
    uint32_t cmd_table_size;
    char *saved_input;                 /* Optional, at least 2 bytes; NULL disables */
    uint32_t saved_buf_size;
    uint32_t flags;                    /* ELIB_CLI_FLAG_* combination */
} elib_cli_cfg_t;

/* --------------------------------------------------------------- context */
/* Statically allocated by the user, initialized by init. The configuration is
 * embedded by value, so the cfg argument may live on the stack. */
typedef struct {
    elib_cli_cfg_t cfg;                /* Copy of the configuration given to init */
    uint32_t cmd_count;                /* Registered commands (0..cmd_table_size) */
    uint32_t rx_pos;                   /* Bytes currently in rx_buf */
    uint32_t saved_len;                /* Bytes kept in saved_input, 0 when none */
    int32_t hist_offset;               /* 0 = editing, n = n-th entry from the end */
    volatile uint8_t escape_state;     /* ANSI escape-sequence decoder state */
    volatile uint8_t crlf_skip_lf;     /* A '\r' was handled; swallow one '\n' */
    volatile uint8_t initialized;      /* init/deinit flag */
} elib_cli_ctx_t;

/* --------------------------------------------------------------- lifecycle */
/**
 * @brief Initialize the CLI
 * @param ctx User-provided context (any historical state; memset-overwritten)
 * @param cfg Configuration (copied by value, no pointer is kept)
 * @return OK / INVALID_PARAM (see the header banner for the rejected cases)
 */
elib_cli_err_t elib_cli_init(elib_cli_ctx_t *ctx, const elib_cli_cfg_t *cfg);

/**
 * @brief Deinitialize the CLI (NULL-safe; every other API rejects afterwards)
 */
void elib_cli_deinit(elib_cli_ctx_t *ctx);

/* --------------------------------------------------------------- commands */
/**
 * @brief Register a command, or replace the one with the same name
 * @param name Case sensitive command name, must not contain spaces
 * @param help Optional one-line description, may be NULL
 * @param callback Handler, mandatory
 * @return OK / INVALID_PARAM / NOT_INITIALIZED / CMD_TABLE_FULL
 */
elib_cli_err_t elib_cli_register(elib_cli_ctx_t *ctx, const char *name,
                                 const char *help, elib_cli_cmd_fn callback);

/* --------------------------------------------------------------- input */
/**
 * @brief Consume one byte from the transport
 * @param ch Byte as received; only 0x08, 0x09, 0x0A, 0x0D, 0x1B, 0x7F and
 *           0x20..0x7E have an effect, everything else is ignored
 * @return OK (also: byte ignored) / INVALID_PARAM / NOT_INITIALIZED /
 *         RX_OVERFLOW (byte dropped, line unchanged) / CMD_NOT_FOUND (the
 *         submitted line matched no command; the caller was already told
 *         through the print callback)
 */
elib_cli_err_t elib_cli_feed_char(elib_cli_ctx_t *ctx, char ch);

/* --------------------------------------------------------------- arguments */
/**
 * @brief Count the space separated arguments of a line
 * @return Number of arguments, 0 for NULL or an all-space string
 */
uint32_t elib_cli_argc(const char *args);

/**
 * @brief Locate the index-th space separated argument
 * @param index 0 based, arguments are separated by runs of spaces
 * @param len Optional out-parameter receiving the argument length in bytes
 * @return Start of the argument, or NULL when index is out of range. The string
 *         is NOT NUL-terminated at the end of the argument: use *len (or copy
 *         the bytes) instead of passing the pointer to printf("%s").
 */
const char *elib_cli_argv(const char *args, uint32_t index, uint32_t *len);

#ifdef __cplusplus
}
#endif

#endif /* ELIB_CLI_H */
