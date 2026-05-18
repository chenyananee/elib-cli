/* elib_cli.h - CLI Library Main Header */
#ifndef ELIB_CLI_H
#define ELIB_CLI_H

#include "elib_cli_err.h"
#include "elib_cli_types.h"

#ifdef __cplusplus
extern "C" {
#endif

elib_cli_err_t elib_cli_init(elib_cli_ctx_t *ctx, const elib_cli_cfg_t *cfg);
void elib_cli_deinit(elib_cli_ctx_t *ctx);
elib_cli_err_t elib_cli_feed_char(elib_cli_ctx_t *ctx, char ch);
elib_cli_err_t elib_cli_register(elib_cli_ctx_t *ctx, const char *name,
                                  const char *help, elib_cli_cmd_fn callback);

int elib_cli_argc(const char *args);
const char *elib_cli_argv(const char *args, int index);

#ifdef __cplusplus
}
#endif

#endif /* ELIB_CLI_H */
