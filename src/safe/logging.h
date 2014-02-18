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

#include <davfuse/logging.h>

#define lbx_log logging_log
#define lbx_log_debug log_debug
#define lbx_log_info log_info
#define lbx_log_warning log_warning
#define lbx_log_error log_error
#define lbx_log_critical log_critical

#endif

#endif
