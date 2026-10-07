#ifndef CONFIG_H
#define CONFIG_H

/** @file config.h
 * @brief Validated runtime configuration and input parsing.
 */

#include "strategy.h"
#include <stdint.h>

enum { GALLERY_MAX_PAINTINGS = 256, GALLERY_MAX_INPUT_LINE = 4096 };

/** @brief Runtime parameters; all times except delay_ms are logical
 * milliseconds.
 * @details config_read() applies defaults, file, environment, CLI, then
 * optional interactive input. Paths are owned arrays, not pointers into input
 * buffers.
 */
typedef struct Config {
    uint64_t capacity, paintings, painting_capacity, visitors;
    uint64_t arrival_min, arrival_max, view_min, view_max;
    uint64_t seed, delay_ms, max_active;
    Strategy strategy;
    int unlimited, quiet, json, interactive;
    char log_path[4096], journal_path[4096];
} Config;

/** @brief Populate and validate configuration from the program arguments.
 * @param[out] config Destination; usable for simulation only on return 0.
 * @param argc Argument count supplied to main.
 * @param argv Argument vector supplied to main.
 * @return 0 on success, 1 after displaying help, -1 for input/I/O failure.
 */
int config_read(Config *config, int argc, char **argv);

#endif
