#ifndef SIMULATION_H
#define SIMULATION_H

/** @file simulation.h
 * @brief Single-threaded event-loop entry point and resource lifetime.
 */

struct Config;
/** @brief Run a day, write results and release all model resources.
 * @param config Immutable configuration validated by config_read().
 * @return 0 on completion, 1 on failure, or 128 + signal on interruption.
 * @pre Signal handlers have been installed with signals_install().
 * @details Incomplete visits are cancelled on interruption or failure.
 * Output failures take precedence over the signal exit status.
 */
int simulate(const struct Config *config);

#endif
