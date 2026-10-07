/** @file signals.c
 * @brief Signal handler installation and interruption-flag ownership.
 */

#include "signals.h"

#include <stdio.h>
#include <string.h>

volatile sig_atomic_t stop_signal = 0;

/* No I/O, allocation or cleanup in the handler: the event loop does that. */
/** @brief Record interruption using only an async-signal-safe assignment.
 * @param number SIGINT or SIGTERM delivered by the installed handler.
 * @details Performs no I/O, allocation or resource cleanup.
 */
static void on_signal(int number) {
    stop_signal = number;
}

/** @details Uses sigaction for both interruption handlers and ignores SIGPIPE
 * so broken console pipes are handled as ordinary output failures.
 */
int signals_install(void) {
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    sigemptyset(&action.sa_mask);
    action.sa_handler = on_signal;
    if (sigaction(SIGINT, &action, NULL) || sigaction(SIGTERM, &action, NULL)) {
        perror("sigaction");
        return -1;
    }
    action.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &action, NULL)) {
        perror("sigaction SIGPIPE");
        return -1;
    }
    return 0;
}
