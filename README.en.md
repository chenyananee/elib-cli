[简体中文](README.md) | **English**

# elib-cli

A zero-dynamic-memory command line interface library for resource-constrained embedded systems: the user statically supplies the context, receive buffer, history buffer, and command table, feeds bytes one at a time, pure C99, single umbrella-header API.

Current version: **2.0.0** (`ELIB_CLI_VERSION`), single header `include/elib_cli.h`.

## Features

- **Zero dynamic memory / no globals**: `ctx`, the receive buffer, the history buffer, the scratch buffer, and the command table are all supplied statically by the user; the library holds no state of its own, and multiple CLI instances can coexist
- **Single umbrella-header API**: all public types and functions live in `elib_cli.h`; no sub-headers
- **One-byte-at-a-time input model**: `elib_cli_feed_char()` consumes a single byte and binds to no IO; the caller moves input in from UART / USB / RTT
- **Escape sequences swallowed whole**: `ESC [ parameters intermediates final` and `ESC O final` are consumed as complete units, so parameterized variants (such as Ctrl+Up's `ESC[1;5A`) can never leak `1;5` into the command line
- **CR/LF inside an escape does not submit**: `0x0D` / `0x0A` appearing in a sequence body never triggers line submission
- **Three configurable newline modes**: LF / CR / CRLF; CRLF mode is forgiving — a single `\r` submits immediately and a `\n` right after it is swallowed, so the terminal never "seems dead after Enter"
- **Whole lines go into history**: the complete command line (arguments included) is submitted, a full buffer evicts a whole old record, and over-long lines are skipped instead of wiping the history
- **Up/down browsing**: the first up-arrow stashes the line being edited, and going down returns to it; an overwritten line is replaced even when it is shorter than the history entry
- **Tab completion**: only active while no space has been typed in the command name; a single match is completed and given a trailing space, multiple matches are listed with the prompt redrawn, and no match does nothing
- **Built-in help**: takes no slot in the user command table, can be overridden by registering the same name, and is also completed by Tab
- **Echo can be turned off**: `ELIB_CLI_FLAG_ECHO` controls whether input is mirrored (characters, backspace, redraw); with it off the library is completely silent and the terminal takes over
- **Six error codes**: `OK / INVALID_PARAM / NOT_INITIALIZED / CMD_TABLE_FULL / RX_OVERFLOW / CMD_NOT_FOUND`
- **Argument parsing helpers**: `elib_cli_argc()` and `elib_cli_argv()` with a length out-parameter
- **Pure C99**, C++ compatible (`extern "C"`)

## Quick Start

### 1. Statically Define the Resources

```c
#include "elib_cli.h"    /* add -I<repo>/include at build time */

static char             g_rx_buf[128];
static char             g_hist_buf[1024];
static char             g_saved_buf[128];
static elib_cli_cmd_t   g_cmd_table[16];
static elib_cli_ctx_t   g_cli;

/* Output callback: printf style, user_data is exactly cfg.user_data.
 * One input byte may trigger several calls (one per echoed character). */
static int cli_print(void *user_data, const char *fmt, ...)
{
    va_list ap;
    int n;

    (void)user_data;
    va_start(ap, fmt);
    n = vprintf(fmt, ap);        /* or UART_Send / SEGGER_RTT_vprintf */
    va_end(ap);
    return n;
}

/* Command callback: args is the text after the command name and is never NULL
 * (it is the empty string when there are no arguments). The pointer refers to
 * the receive buffer and is valid only during this call. */
static void cmd_led(void *user_data, const char *args)
{
    uint32_t len = 0u;
    const char *arg0 = elib_cli_argv(args, 0u, &len);

    (void)user_data;
    if (arg0 != NULL && len == 2u && strncmp(arg0, "on", 2u) == 0) {
        LED_On();
    } else {
        LED_Off();
    }
    /* Do not write the line ending here: the library appends the terminator
     * after the callback returns. */
}
```

### 2. Initialize

```c
static void app_cli_init(void)
{
    elib_cli_cfg_t cfg = {
        .prompt           = "> ",                 /* may be NULL: no prompt is printed */
        .newline          = ELIB_CLI_NL_CRLF,     /* LF / CR / CRLF */
        .print            = cli_print,            /* required */
        .user_data        = NULL,                 /* passed through to both callbacks */
        .rx_buf           = g_rx_buf,             /* required, at least 2 bytes */
        .rx_buf_size      = sizeof(g_rx_buf),
        .history_buf      = g_hist_buf,           /* optional, at least 3 bytes */
        .history_buf_size = sizeof(g_hist_buf),
        .cmd_table        = g_cmd_table,          /* required, at least 1 slot */
        .cmd_table_size   = 16u,
        .saved_input      = g_saved_buf,          /* optional, at least 2 bytes */
        .saved_buf_size   = sizeof(g_saved_buf),
        .flags            = ELIB_CLI_FLAG_ECHO,   /* mirror the input */
    };

    /* cfg is copied by value and may live on the stack; the buffers and the
     * command table it points to must outlive the context */
    elib_cli_init(&g_cli, &cfg);

    elib_cli_register(&g_cli, "led",    "Control LED", cmd_led);
    elib_cli_register(&g_cli, "reboot", "Reboot system", cmd_reboot);
}
```

`init` only prepares the resources and **does not print an initial prompt** — if one is wanted, the caller prints it itself once the transport layer is ready.

### 3. Feed Bytes

```c
for (;;) {
    if (uart_has_data()) {
        (void)elib_cli_feed_char(&g_cli, (char)uart_getc());
    }
}
```

### 4. Deinitialize

```c
elib_cli_deinit(&g_cli);   /* afterwards every API except init returns NOT_INITIALIZED */
```

## API Overview

| Group | Functions | Description |
|------|------|------|
| Lifetime | `elib_cli_init(ctx, cfg)` | Initialize (memsets ctx; copies cfg by value; rejects illegal configurations) |
| | `elib_cli_deinit(ctx)` | Deinitialize (NULL-safe; afterwards everything except init is rejected) |
| Commands | `elib_cli_register(ctx, name, help, callback)` | Register a command; an existing name is replaced in place |
| Input | `elib_cli_feed_char(ctx, ch)` | Consume one byte (the only input entry point) |
| Arguments | `elib_cli_argc(args)` | Count arguments separated by spaces |
| | `elib_cli_argv(args, index, &len)` | Locate the index-th argument and report its length |

Error codes (6, `elib_cli_err_t`):

| Error code | Meaning |
|--------|------|
| `ELIB_CLI_OK` | Success; **also covers ignored bytes** (control characters, an unmatched Tab, ...) |
| `ELIB_CLI_ERR_INVALID_PARAM` | NULL pointer, illegal newline value, unknown flag, buffer too small, incomplete arguments |
| `ELIB_CLI_ERR_NOT_INITIALIZED` | Not `init`ed, or already `deinit`ed |
| `ELIB_CLI_ERR_CMD_TABLE_FULL` | The command table is full and cannot take a new command (replacement is unaffected) |
| `ELIB_CLI_ERR_RX_OVERFLOW` | The current line is full, this byte was dropped, and the line content is unchanged |
| `ELIB_CLI_ERR_CMD_NOT_FOUND` | The submitted line matched no command (the user already learned this through `print`) |

`feed_char` returns the result of processing **this single byte**: submitting an unknown command returns `CMD_NOT_FOUND`, everything else returns `OK`.

## Input Model

`elib_cli_feed_char()` recognises only the bytes below and ignores everything else (it does not enter the line, does not echo, and reports no error):

| Byte | Meaning |
|------|------|
| `0x20`..`0x7E` | Printable characters: appended to the line, mirrored when echo is on |
| `0x08`, `0x7F` | Backspace: deletes the last byte, prints `\b \b` when echo is on |
| `0x09` | Tab: triggers command-name completion |
| Newline | Submits the current line; which byte exactly is decided by `cfg.newline` |
| `0x1B` | Start of an ANSI escape sequence, see below |

> Line content is always a NUL-terminated printable string: `strlen(rx_buf) == rx_pos`, and `rx_pos <= rx_buf_size - 1`.

### Escape Sequences

```
ESC [ <parameters 0x30..0x3F>* <intermediates 0x20..0x2F>* <final 0x40..0x7E>
ESC O <final>
```

- Only the final bytes `A` / `B` have an action (browse history up / down); every other sequence is swallowed whole
- Parameterized variants (`ESC[1;5A`, `ESC[3~`, `ESC[2J`) are never injected into the command name
- A lone `ESC` swallows only the one byte that immediately follows it
- `0x0D` / `0x0A` in a sequence body are **not submitted**; if they are one of the final bytes they end the whole sequence

Byte-by-byte samples (`rx` is the input, `line` is the final line content):

| Input | line | Description |
|------|------|------|
| `ab ESC [ 2 J cd` | `abcd` | The clear-screen sequence is swallowed whole |
| `ab ESC [ 3 ~ c` | `abc` | The Delete key is swallowed whole |
| `ab ESC x cd` | `abcd` | A lone ESC swallows one byte |
| `ESC [ 1 ; 5 A` | (empty) | Parameterized up arrow = browse up once |
| `ESC O A` | (empty) | SS3 up arrow = browse up once |

## Newline Semantics

| `cfg.newline` | Submission condition | Terminator printed by the library |
|---------------|----------|--------------------|
| `ELIB_CLI_NL_LF` | `\n` | `\n` |
| `ELIB_CLI_NL_CR` | `\r` | `\r` |
| `ELIB_CLI_NL_CRLF` | `\r` (immediately), or `\r\n` | `\r\n` |

Newline bytes that do not belong to the current mode are **ignored**: they are not entered, not echoed, and not submitted. In LF mode, for example, a received `\r` does nothing at all.

CRLF mode is "forgiving" and has only two rules:

1. On `\r` **submit immediately**, without waiting for the next byte (a terminal that only sends CR does not look like a dead key)
2. After submitting, swallow the single `\n` that follows; if the next byte is not `\n` it is interpreted as usual (for example `"led\rX"` submits `led` and `X` then starts a new line)

Therefore:

| Input (CRLF mode) | Submissions | Description |
|--------------------|----------|------|
| `led \r \n` | 1 | Standard CRLF |
| `led \r` | 1 | CR only, submitted immediately |
| `led \r \n \n \n` | 1 | The extra LFs are ignored |
| `led \r \r` | 2 | The second CR submits an empty line |
| `led \n` | 0 | A lone LF is meaningless |

The history buffer **always** uses `\n` as its separator, regardless of the newline mode.

## Command Line Semantics

On submission the line is split on **single spaces**:

- After leading spaces are stripped, the first token is the command name, case-sensitive
- Everything after the command name (leading spaces removed, inner and trailing spaces kept) is passed to the callback as `args` verbatim
- An empty line (or an all-space line) only reprints the prompt: no callback is called and nothing is written to history
- An unknown command prints `unknown command: <name>`, and `feed_char` returns `CMD_NOT_FOUND`

```
input:  "  led   on  now \n"
name:   "led"
args:   "on  now "        ← leading spaces removed, middle/trailing spaces kept
```

### Built-in help

- Lists the command table (registration order), one `name` per line, or aligned with `%-12s %s` when help text exists, and finally lists the built-in `help` itself
- **Takes no** slot in the user command table, and `ctx.cmd_count` does not grow because of it
- Can be overridden with `elib_cli_register(ctx, "help", ...)`; from then on the built-in listing never appears again
- Arguments are ignored (`help whatever` is equivalent to `help`)

## History

- Every submission appends the **whole line** (command name + arguments) to `history_buf` as `<line>\n`
- Empty lines are not recorded; consecutive identical lines are recorded repeatedly (deduplication is left to the caller)
- When space runs out the **entire** oldest record is evicted; half a record is never left behind
- A line that does not fit even into an empty buffer is **skipped** and the history stays as it was (it is not cleared)
- Up/down arrows browse by "offset from the most recent entry"; the first up-arrow stores the line being edited in `saved_input`, and going down back to offset 0 restores it
- A too-small `saved_input` truncates the stashed content but never overflows
- Without `history_buf`, or without `saved_input`, the related features degrade quietly (up/down have no effect)

The shape of the history buffer always satisfies: `len <= history_buf_size`, ends with `\n`, and contains no empty records (`"\n\n"`).

## Completion

Tab is only active while **no space has been typed into the command name**; once there is a space the cursor is in the argument area and Tab does nothing.

| Case | Behavior |
|------|------|
| A single match (built-in help included) | The whole line is replaced with `name` + one space; with echo on the old content is erased first and the new content printed |
| Multiple matches | Newline, the candidates listed one per line (user table first, built-in help last), then the prompt and the current line reprinted |
| No match | Nothing happens, returns `OK` |
| Does not fit after completion (`name length + 2 > rx_buf_size`) | Completion is abandoned and the line is unchanged |
| The user registered `help` | The built-in help no longer participates in completion and is not listed twice |

## Echo

| `flags` | Behavior |
|---------|------|
| Contains `ELIB_CLI_FLAG_ECHO` | Mirrors what the library consumes: every accepted printable character, `\b \b` for every backspace, the redraw after history/completion |
| Does not contain it | No input echo at all; submission still prints the terminator and the prompt, and `unknown command` and the help list are still printed |

Echo only affects "what was printed"; it does not affect the line content, the history, or the completion result.

## Callback Contract and Concurrency

```c
typedef int  (*elib_cli_print_fn)(void *user_data, const char *fmt, ...);
typedef void (*elib_cli_cmd_fn)(void *user_data, const char *args);
```

- `args` is **never NULL** and is the empty string when there are no arguments; it points into the receive buffer and is valid only during the call (copy it if you need to keep it)
- `print` may be called several times for one input byte and must be non-blocking; anything sent asynchronously must be copied inside the callback first
- `user_data` is passed through from `cfg`, and both callbacks receive the same pointer — so commands need no globals of their own
- A command callback **must not** call `feed_char()` / `register()` / `deinit()` on the same ctx: `feed_char` mutates line state and is not reentrant
- The library appends the terminator and reprints the prompt after the callback returns, so callback output **must not** write its own line ending
- When a callback needs to output something, go through the same `print`, otherwise the ordering breaks

`feed_char` mutates shared line state and is therefore **not** reentrant: feed from a single place (the main loop or one ISR, pick one), never from both at once, and never from inside a callback.

## Lifetime and Memory

- `init` **unconditionally** memsets `ctx`: any previous state can be reinitialized
- `cfg` is a **value struct**: copied field by field, with one copy embedded in `ctx.cfg`; no pointer to `cfg` is kept, so `cfg` can live on the stack
- `init` clears `rx_buf[0]`, `history_buf[0]`, and `saved_input[0]` so no stale bytes are parsed
- All buffers and the command table must **outlive** the context (lifetime ≥ the context's); the `name` / `help` strings must stay valid just as long (the library only stores pointers)
- `init` performs all validation before touching `ctx`: when the configuration is rejected, not a single byte of `ctx` changes
- `deinit` only clears the runtime state; the configuration is still readable, but every API except `init` returns `NOT_INITIALIZED`
- Re-running `init` replaces the configuration, the command table count, the history content, and the line state wholesale

## Building

Add `include/` to the header search path and compile the source file as an ordinary C99 object file:

```bash
arm-none-eabi-gcc -std=c99 -Wall -Wextra -Werror -Wpedantic -Iinclude \
    -c src/elib_cli_core.c -o elib_cli_core.o
```

The source file includes the umbrella header directly (`#include "elib_cli.h"`), depends on no relative paths, and ships no build system of its own.

## Testing

`test/` holds 12 independent test files (162 cases in total); each has its own `main()` and can be compiled and run separately:

```bash
for t in test/test_*.c; do
    gcc -std=c99 -Wall -Wextra -Werror -Wpedantic -Iinclude \
        "$t" src/elib_cli_core.c -o "${t%.c}.exe"
    "./${t%.c}.exe"
done
```

| File | Coverage |
|------|------|
| `test_init.c` | Version macros and minimum-buffer macros, cfg copied field by field, a stack cfg may be destroyed, ten kinds of illegal configuration rejected (including half-configured buffers, out-of-range newline, unknown flags), ctx unchanged when the configuration is rejected, dirty ctx reinitialization, `init` clears the user buffers and prints nothing |
| `test_register.c` | Parameter validation, rejection before init / after deinit, registration order, in-place replacement, case sensitivity, `CMD_TABLE_FULL` when the table is full, replacement still possible when full, `help` taking an ordinary slot, reinitialization clears the table |
| `test_line_edit.c` | Both ends of the printable range, backspace and DEL, silent backspace on an empty line, 15 control bytes ignored, the `RX_OVERFLOW` boundary and recovery, the buffer always NUL-terminated |
| `test_execute.c` | Dispatch with and without arguments, leading spaces stripped, inner and trailing spaces kept, empty and all-space lines, the unknown-command text and `CMD_NOT_FOUND`, case sensitivity, `user_data` pass-through, state reset after submission, history gets the whole line, callback output order |
| `test_args.c` | `argc` over the various space shapes, `argv` location and length out-parameter, out of range and NULL, `len` may be omitted, the contract that `p[len]` is not NUL, `argv` agrees with `argc` |
| `test_tab.c` | A single match completed with a trailing space, silence on no match, the multi-match list and redraw, built-in help participating in completion, Tab ineffective in the argument area (`led ` and `led on` included), capacity boundaries (exactly fits / does not fit), no duplication after `help` is overridden, completion and listing with echo off |
| `test_history.c` | Whole-line records, empty lines not recorded, up-browsing order and limit, redraw erasing, down-browsing restoring the stashed line, stash buffer truncation, no history buffer, whole-record eviction, over-long lines skipped, duplicate lines kept, the cursor reset on submission, buffer shape invariants |
| `test_escape.c` | CSI / SS3 up and down arrows, parameterized Ctrl+Up, clear-screen and Delete sequences, a lone ESC swallowing one byte, incomplete CSI, intermediate bytes, CR/LF/CRLF inside a sequence not submitting, a NUL ending a sequence, SS3 function keys, empty history |
| `test_newline.c` | Submission and ignore rules for LF / CR / CRLF, a single CR submitting immediately in CRLF mode, swallowing the paired LF, keeping the bytes after CR, consecutive terminators, the printed terminator following the mode, the terminator not entering the line, reinitialization switching modes |
| `test_echo.c` | The three kinds of mirroring with echo on, input/backspace/history/completion silent but state correct with echo off, submission still printing the terminator and the prompt, unknown commands still reported, never printing a backspace with echo off, echo only affecting output |
| `test_lifecycle.c` | `deinit` NULL-safe, clearing the runtime state, repeated `deinit`, everything rejected after death, NULL ctx, a zeroed ctx, the configuration still readable, re-init reviving, reinitialization discarding the session, **two contexts not interfering**, the decoder re-armed |
| `test_help.c` | List order and `%-12s` alignment (including a 12-character name needing no padding), an empty table listing only itself, taking no slot, ignoring arguments, returning `OK`, being overridden, still listing with echo off, no stray blank lines, case sensitivity |

### Cross-Checking Against the Reference Model

Besides the assertions above, the behavior is also cross-checked against an independent Python reference model (not part of the build, used for verification only):

- **Model self-consistency**: `64165` assertions, including 34 byte-by-byte scenarios (each independently recomputing the output, line content, return value, and callback records), a full cross-check of two structurally different implementations (a byte-by-byte state machine vs. whole-segment look-ahead scanning) over `2 flags × 3 newline modes × 4 history × 4 stash × 4 command tables × 20 inputs` = `7680` combinations, and invariants over 3000 random inputs (the line never overflows, the stash never overflows, the history ends with `\n` and has no empty records, offsets stay in range, no orphan backspace appears when echo is off)
- **C ↔ model cross-check**: the real implementation is compiled for the host and compared field by field against the model over `35428` input groups (`212568` assertions); the fields include output bytes, line content, history content, offsets, each callback's name/arguments/`argc`/`argv`, and the last return value of `feed_char`

All results are `TOTAL FAILS: 0`.

## License

MIT License (see `LICENSE`).
