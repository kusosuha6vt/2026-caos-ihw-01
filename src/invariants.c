/** @file invariants.c
 * @brief Runtime cross-checks of visitor history, queues and occupancy.
 */

#include "invariants.h"

#include "internal/model.h"

#include <string.h>

/* Check queue membership as well as counters: redundant state can disagree. */
/** @brief Validate one FIFO and mark its visitors in membership scratch
 * storage.
 * @param[in,out] s Model whose membership scratch array is updated.
 * @param q FIFO to check without changing its links.
 * @param state Required state of every member.
 * @param painting Required painting index for PAINTING_WAIT, otherwise ignored.
 * @return 1 if links, length, tail and ownership agree, otherwise 0.
 * @details Detects invalid slots, cycles and duplicate queue membership.
 */
static int check_queue(Simulation *s, Queue *q, State state, int painting) {
    uint64_t length = 0;
    int last = -1;
    for (int i = q->head; i >= 0; i = s->visitors[i].next) {
        if (i >= s->slots || ++length > (uint64_t)s->slots || s->membership[i])
            return 0;
        Visitor *v = &s->visitors[i];
        if (v->state != state ||
            (state == PAINTING_WAIT && v->painting != painting))
            return 0;
        s->membership[i] = 1;
        last = i;
    }
    return length == q->length && last == q->tail && ((q->head < 0) == !length);
}

/** @details Reconstructs active/inside/viewing counts and seen totals from
 * records, then compares them with stored counters and configured capacities.
 */
int simulation_check(Simulation *simulation) {
    memset(simulation->membership, 0, (size_t)simulation->slots);
    if (!check_queue(simulation, &simulation->entrance, ENTRANCE, -1))
        return 0;
    uint64_t viewing[GALLERY_MAX_PAINTINGS] = {0}, inside = 0, active = 0;
    for (uint64_t p = 0; p < simulation->config->paintings; ++p)
        if (!check_queue(simulation, &simulation->paintings[p].queue,
                         PAINTING_WAIT, (int)p))
            return 0;
    for (int i = 0; i < simulation->slots; ++i) {
        Visitor *v = &simulation->visitors[i];
        if (v->state == FREE)
            continue;
        ++active;
        if (v->state != ENTRANCE)
            ++inside;
        if ((v->state == ENTRANCE || v->state == PAINTING_WAIT) !=
            !!simulation->membership[i])
            return 0;
        uint64_t seen = 0;
        for (uint64_t p = 0; p < simulation->config->paintings; ++p)
            seen += v->seen[p];
        if (seen != v->viewed || seen > simulation->config->paintings)
            return 0;
        if (v->state == VIEWING || v->state == PAINTING_WAIT) {
            if (v->painting < 0 ||
                (uint64_t)v->painting >= simulation->config->paintings ||
                v->seen[v->painting])
                return 0;
            if (v->state == VIEWING) {
                if (v->finish < simulation->now)
                    return 0;
                ++viewing[v->painting];
            }
        }
    }
    for (uint64_t p = 0; p < simulation->config->paintings; ++p)
        if (viewing[p] != simulation->paintings[p].viewers ||
            viewing[p] > simulation->config->painting_capacity)
            return 0;
    return inside == simulation->gallery &&
           inside <= simulation->config->capacity &&
           active == simulation->active &&
           simulation->arrived ==
               simulation->completed + simulation->cancelled + active;
}
