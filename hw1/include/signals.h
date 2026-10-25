#ifndef SIGNALS_H
#define SIGNALS_H

/** @file signals.h
 * @brief Async-signal-safe interruption flag and handler setup.
 */

#include <signal.h>

/** @brief Zero until a handler records SIGINT or SIGTERM.
 * @details The handler only assigns this flag; the event loop owns cleanup.
 */
extern volatile sig_atomic_t stop_signal;
/** @brief Install SIGINT/SIGTERM handlers and ignore SIGPIPE.
 * @return 0 on success, -1 after reporting a sigaction failure to stderr.
 */
int signals_install(void);

#endif
