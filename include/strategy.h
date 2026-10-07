#ifndef STRATEGY_H
#define STRATEGY_H

/** @file strategy.h
 * @brief Fixed RNG inputs and painting-choice strategies.
 */

#include <stdint.h>

/** @brief Painting traversal mode, validated during input parsing. */
typedef enum { ORDERED, RANDOM, LEAST_CROWDED, CYCLIC } Strategy;
struct Config;
struct Visitor;
struct Simulation;

/** @brief Return a stable CLI name for a valid strategy.
 * @param strategy One of the Strategy enumerators.
 * @return Borrowed immutable string with static lifetime.
 */
const char *strategy_name(Strategy strategy);
/** @brief Sample a logical arrival interval independently of choice order.
 * @param config Validated seed and arrival bounds.
 * @param id Visitor ID (starting at 1).
 * @return Interval in the inclusive configured range, in milliseconds.
 */
uint64_t arrival_interval(const struct Config *config, uint64_t id);
/** @brief Sample viewing duration keyed by seed, visitor and painting.
 * @param config Validated seed and viewing bounds.
 * @param id Visitor ID (starting at 1).
 * @param painting Zero-based painting index within the configured range.
 * @return Duration in the inclusive configured range, in milliseconds.
 */
uint64_t viewing_duration(const struct Config *config, uint64_t id,
                          int painting);
/** @brief Initialize private RNG and cyclic traversal parameters.
 * @param config Validated configuration with at least one painting.
 * @param[in,out] visitor Record with its unique ID already assigned.
 */
void strategy_init(const struct Config *config, struct Visitor *visitor);
/** @brief Choose the next unfinished painting.
 * @param simulation Initialized model, observed without modification.
 * @param[in,out] visitor Active visitor; private choice RNG may advance.
 * @return Zero-based painting index.
 * @pre At least one painting is unfinished and strategy_init() has been called.
 */
int choose_painting(const struct Simulation *simulation,
                    struct Visitor *visitor);

#endif
