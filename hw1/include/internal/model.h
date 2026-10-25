#ifndef MODEL_H
#define MODEL_H

/** @file internal/model.h
 * @brief Private records shared by synchronous model helper implementations.
 * @details This type-only header is not an independent module interface.
 */

#include "config.h"

/* Private model records: all mutable state is owned by the event loop.
 * Supporting modules run synchronously and never schedule model transitions. */
/** @brief Visitor-slot state; FREE slots can be reused with a new visitor ID.
 */
typedef enum { FREE, ENTRANCE, READY, PAINTING_WAIT, VIEWING } State;
/** @brief Intrusive FIFO of visitor slots; -1 denotes an empty head/tail. */
typedef struct {
    int head, tail;
    uint64_t length;
} Queue;

/** @brief One visitor record, reused after departure with a new unique ID.
 * @details seen is an owned per-painting byte map. next links one FIFO only;
 * viewing reserves the selected painting until the logical finish timestamp.
 */
typedef struct Visitor {
    State state;
    uint64_t id, viewed, wait_since, finish, rng, start, step;
    int painting, next;
    unsigned char *seen;
} Visitor;

/** @brief Painting reservations, FIFO and occupancy-time integral. */
typedef struct {
    uint64_t viewers, peak, started, finished;
    long double area;
    Queue queue;
} Painting;

/** @brief All state owned by one event loop for one simulated day.
 * @details config is borrowed for the run. Visitor/painting arrays and
 * membership scratch storage are owned; output functions manage log_fd, seq and
 * io_failed. Areas integrate occupancy over logical milliseconds, not display
 * time.
 */
typedef struct Simulation {
    const Config *config;
    Visitor *visitors;
    Painting *paintings;
    unsigned char *membership;
    int slots, log_fd, io_failed;
    uint64_t now, seq, arrived, entered, completed, cancelled, active;
    uint64_t gallery, peak_gallery, views_started, views_finished;
    uint64_t entrance_wait_sum, entrance_wait_max, painting_wait_sum,
        painting_wait_max;
    long double gallery_area;
    Queue entrance;
} Simulation;

#endif
