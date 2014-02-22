#ifndef _safe_logging_h
#define _safe_logging_h

/* we just use the logging system provided by davfuse for now,
   TODO: all libraries should be configured to use the same common
   logging backend */

#ifdef SFX_EMBEDDED

#define lbx_log(...) do { } while(0)
#define lbx_log_debug(...) do { } while(0)
#define lbx_log_info(...) do { } while(0)
#define lbx_log_warning(...) do { } while(0)
#define lbx_log_error(...) do { } while(0)
#define lbx_log_critical(...) do { } while(0)

#else

#include <assert.h>

#include <davfuse/logging.h>
#include <encfs/base/logging.h>

#define lbx_log logging_log
#define lbx_log_debug log_debug
#define lbx_log_info log_info
#define lbx_log_warning log_warning
#define lbx_log_error log_error
#define lbx_log_critical log_critical

#ifdef __cplusplus

inline
log_level_t
encfs_log_level_to_davfuse_log_level(encfs_log_level_t level) {
  switch (level) {
  case ENCFS_LOG_DEBUG: return LOG_DEBUG;
  case ENCFS_LOG_INFO: return LOG_INFO;
  case ENCFS_LOG_WARNING: return LOG_WARNING;
  case ENCFS_LOG_ERROR: return LOG_ERROR;
  default: assert(false); return LOG_DEBUG;
  }
}

inline
void
encfs_log_printer_adapter(const char *filename, int lineno, encfs_log_level_t level,
                          const char *s) {
  log_printer_print(filename, lineno, encfs_log_level_to_davfuse_log_level(level), s);
}

#endif

#endif

#endif
