/** @file main.c
 * @brief Program composition and exit-status handling.
 */

#include "config.h"
#include "signals.h"
#include "simulation.h"

/** @brief Initialize signals, parse configuration and run the model.
 * @param argc Argument count supplied by the operating system.
 * @param argv Argument vector supplied by the operating system.
 * @return 0 on completion/help, 2 on invalid input, 1 on runtime failure,
 * or 128 + signal on interruption.
 */
int main(int argc, char **argv) {
    if (signals_install())
        return 1;
    Config config;
    int result = config_read(&config, argc, argv);
    if (stop_signal)
        return 128 + stop_signal;
    if (result != 0)
        return result > 0 ? 0 : 2;
    return simulate(&config);
}
