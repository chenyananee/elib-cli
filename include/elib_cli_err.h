/* elib_cli_err.h - CLI Error Codes */
#ifndef ELIB_CLI_ERR_H
#define ELIB_CLI_ERR_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ELIB_CLI_OK = 0,
    ELIB_CLI_ERR_INVALID_PARAM,
    ELIB_CLI_ERR_NOT_INITIALIZED,
    ELIB_CLI_ERR_CMD_TABLE_FULL,
    ELIB_CLI_ERR_CMD_EXISTS,
    ELIB_CLI_ERR_CMD_NOT_FOUND,
    ELIB_CLI_ERR_RX_OVERFLOW,
} elib_cli_err_t;

#ifdef __cplusplus
}
#endif

#endif /* ELIB_CLI_ERR_H */
