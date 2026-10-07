#ifndef OUTPUT_H
#define OUTPUT_H

/** @file output.h
 * @brief Synchronous event output, journal and summary formatting.
 */

#include <stdint.h>

struct Simulation;
/** @brief Open the event log, rejecting existing log/journal aliases.
 * @param[in,out] simulation Initialized model with log_fd set to -1.
 * @return 0 on success (including disabled log), -1 after reporting failure.
 */
int output_open(struct Simulation *simulation);
/** @brief Close the event log if enabled.
 * @param[in,out] simulation Model holding the log descriptor.
 * @return 0 on success, -1 after reporting a close failure.
 */
int output_close(struct Simulation *simulation);
/** @brief Write one event snapshot and apply the configured display delay.
 * @param[in,out] simulation Current model; sequence and I/O status may change.
 * @param kind Internal JSON-safe event name.
 * @param message Internal JSON-safe human description.
 * @param slot Valid visitor slot, or -1 for no visitor.
 * @param painting Zero-based painting index, or -1 for no painting.
 * @param duration Event duration in logical milliseconds, or 0 if unused.
 * @details Writes are synchronous; failures set the model's io_failed flag.
 * Strings must need no JSON escaping. Pacing does not change logical time.
 */
void output_event(struct Simulation *simulation, const char *kind,
                  const char *message, int slot, int painting,
                  uint64_t duration);
/** @brief Write the initial configuration record (sequence 0).
 * @param[in,out] simulation Initialized model; write failures set io_failed.
 */
void output_config(struct Simulation *simulation);
/** @brief Write journal, painting statistics and the final summary.
 * @param[in,out] simulation Model after all visits have ended or been
 * cancelled.
 * @param status Internal status: completed, interrupted or failed.
 * @param pending_entrance Censored entrance-wait sum measured before
 * cancellation.
 * @param pending_painting Censored painting-wait sum measured before
 * cancellation.
 * @details Wait sums are logical milliseconds. I/O failures set io_failed and
 * change the emitted summary status to failed.
 */
void output_summary(struct Simulation *simulation, const char *status,
                    uint64_t pending_entrance, uint64_t pending_painting);

#endif
