#ifndef _RUN_ENCFS_H
#define _RUN_ENCFS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  RUN_ENCFS_ERROR_NONE,
  RUN_ENCFS_ERROR_IS_NOT_DIRECTORY,
  RUN_ENCFS_ERROR_PASSWORD_INCORRECT,
  RUN_ENCFS_ERROR_GENERAL,
} run_encfs_error_t;

void
init_encfs(void);

void
deinit_encfs(void);

run_encfs_error_t
run_encfs(const char *encrypted_path, char *password);

#ifdef __cplusplus
}
#endif

#endif
