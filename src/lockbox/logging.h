#ifndef _lockbox_logging_h
#define _lockbox_logging_h

/* we just use the logging system provided by davfuse for now,
   TODO: all libraries should be configured to use the same common
   logging backend */

#include <davfuse/logging.h>

#define lbx_log logging_log
#define lbx_log_debug log_debug
#define lbx_log_info log_info
#define lbx_log_warning log_warning
#define lbx_log_error log_error
#define lbx_log_critical log_critical

#endif
