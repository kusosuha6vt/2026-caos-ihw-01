#ifndef INVARIANTS_H
#define INVARIANTS_H

/** @file invariants.h
 * @brief Runtime cross-checks for model state and queue consistency.
 */

struct Simulation;
/** @brief Check capacities, queue membership, visit counts and conservation.
 * @param[in,out] simulation Initialized model; membership is scratch storage.
 * @return 1 if consistent, 0 if an invariant is violated.
 * @details Does not change visitor states, occupancy or queue links.
 */
int simulation_check(struct Simulation *simulation);

#endif
