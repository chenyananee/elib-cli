/* elib_cli_types.h - CLI Type Definitions */
#ifndef ELIB_CLI_TYPES_H
#define ELIB_CLI_TYPES_H

#include <stdint.h>
#include "elib_cli_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Callbacks */
typedef void (*elib_cli_cmd_fn)(const char *args);
typedef int  (*elib_cli_print_fn)(const char *fmt, ...);

/* Newline type */
typedef enum {
    ELIB_CLI_NL_LF = 0,
    ELIB_CLI_NL_CR,
    ELIB_CLI_NL_CRLF,
} elib_cli_nl_t;

/* Command descriptor */
typedef struct {
    const char *name;
    const char *help;
    elib_cli_cmd_fn callback;
} elib_cli_cmd_t;

/* Configuration - all resources user-allocated */
typedef struct {
    const char *prompt;
    elib_cli_nl_t newline;
    elib_cli_print_fn print;
    char *rx_buf;
    uint16_t rx_buf_size;
    char *history_buf;
    uint16_t history_buf_size;
    elib_cli_cmd_t *cmd_table;
    uint8_t cmd_table_size;
    char *saved_input;
    uint16_t saved_buf_size;
    int echo;
} elib_cli_cfg_t;

/* Main context */
typedef struct {
    const elib_cli_cfg_t *cfg;
    uint8_t cmd_count;
    uint16_t rx_pos;
    uint16_t saved_len;
    int hist_offset;
    uint8_t escape_state;
    uint8_t cr_pending;
    int initialized;
} elib_cli_ctx_t;

#ifdef __cplusplus
}
#endif

#endif /* ELIB_CLI_TYPES_H */
