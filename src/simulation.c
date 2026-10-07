/** @file simulation.c
 * @brief FIFO mutations, visitor transitions and deterministic event dispatch.
 */

#include "simulation.h"

#include "internal/model.h"
#include "invariants.h"
#include "output.h"
#include "signals.h"
#include "strategy.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Construct the empty intrusive-FIFO sentinel.
 * @return Queue with head/tail -1 and length 0.
 */
static Queue empty_queue(void) {
    return (Queue){.head = -1, .tail = -1};
}

/* Each visitor can wait in only one FIFO, so its slot is also the queue node.
 */
/** @brief Append a visitor slot to one intrusive FIFO.
 * @param[in,out] s Model holding the visitor-link array.
 * @param[in,out] queue Destination FIFO.
 * @param slot Valid visitor slot not currently linked in another FIFO.
 * @details A visitor can wait in only one FIFO; its next member is the queue
 * node.
 */
static void push(Simulation *s, Queue *queue, int slot) {
    s->visitors[slot].next = -1;
    if (queue->tail >= 0)
        s->visitors[queue->tail].next = slot;
    else
        queue->head = slot;
    queue->tail = slot;
    ++queue->length;
}

/** @brief Remove the first visitor slot from an intrusive FIFO.
 * @param[in,out] s Model holding the visitor-link array.
 * @param[in,out] queue Nonempty FIFO.
 * @return Removed slot, with its next link reset to -1.
 * @pre queue has at least one member.
 */
static int pop(Simulation *s, Queue *queue) {
    int slot = queue->head;
    queue->head = s->visitors[slot].next;
    if (queue->head < 0)
        queue->tail = -1;
    s->visitors[slot].next = -1;
    --queue->length;
    return slot;
}

/* Integration occurs before changing state at the next logical timestamp. */
/** @brief Integrate occupancy and advance the logical clock.
 * @param[in,out] s Model with the occupancy of the preceding interval.
 * @param time Next logical timestamp, not earlier than the current time.
 * @details Integrates before any transition changes occupancy.
 */
static void advance(Simulation *s, uint64_t time) {
    uint64_t dt = time - s->now;
    s->gallery_area += (long double)s->gallery * (long double)dt;
    for (uint64_t p = 0; p < s->config->paintings; ++p)
        s->paintings[p].area +=
            (long double)s->paintings[p].viewers * (long double)dt;
    s->now = time;
}

/** @brief Start painting-FIFO waiters while reservations are available.
 * @param[in,out] s Model owning records, occupancy and wait statistics.
 * @param painting Valid zero-based painting index.
 * @details Stops on interruption/output failure; each started visit reserves
 * a place through its scheduled viewing completion.
 */
static void start_waiters(Simulation *s, int painting) {
    Painting *p = &s->paintings[painting];
    while (p->queue.length && p->viewers < s->config->painting_capacity &&
           !stop_signal && !s->io_failed) {
        int slot = pop(s, &p->queue);
        Visitor *v = &s->visitors[slot];
        uint64_t wait = s->now - v->wait_since;
        uint64_t duration = viewing_duration(s->config, v->id, painting);
        v->state = VIEWING;
        v->finish = s->now + duration;
        ++p->viewers;
        if (p->viewers > p->peak)
            p->peak = p->viewers;
        ++p->started;
        ++s->views_started;
        s->painting_wait_sum += wait;
        if (wait > s->painting_wait_max)
            s->painting_wait_max = wait;
        output_event(s, "view_start",
                     "Начало осмотра; место занято до окончания", slot,
                     painting, duration);
    }
}

/** @brief Finish a complete visitor or enqueue its next painting choice.
 * @param[in,out] s Model receiving queue/occupancy transitions.
 * @param slot Valid visitor slot in READY state.
 * @details A completed visitor releases gallery capacity; otherwise existing
 * painting waiters retain FIFO priority over the new choice.
 */
static void choose_or_leave(Simulation *s, int slot) {
    Visitor *v = &s->visitors[slot];
    if (v->viewed == s->config->paintings) {
        --s->gallery;
        --s->active;
        ++s->completed;
        output_event(s, "exit",
                     "Все картины осмотрены; место в галерее освобождено", slot,
                     -1, 0);
        v->state = FREE;
        return;
    }
    if (stop_signal || s->io_failed)
        return;
    int painting = choose_painting(s, v);
    v->painting = painting;
    output_event(s, "choose", "Выбрана следующая неосмотренная картина", slot,
                 painting, 0);
    v->state = PAINTING_WAIT;
    v->wait_since = s->now;
    push(s, &s->paintings[painting].queue, slot);
    if (s->paintings[painting].queue.head != slot ||
        s->paintings[painting].viewers == s->config->painting_capacity)
        output_event(s, "painting_wait",
                     "Ожидание свободного места в FIFO картины", slot, painting,
                     0);
    start_waiters(s, painting);
}

/* The watchman is a model function, not an OS thread/process. */
/** @brief Model the watchman admitting entrance-FIFO waiters.
 * @param[in,out] s Model with entrance queue and gallery reservations.
 * @details Emits permission/entry events and starts each admitted visitor's
 * next choice. Stops when full or on interruption/output failure.
 */
static void admit(Simulation *s) {
    while (s->entrance.length && s->gallery < s->config->capacity &&
           !stop_signal && !s->io_failed) {
        int slot = pop(s, &s->entrance);
        Visitor *v = &s->visitors[slot];
        output_event(s, "permission", "Вахтёр разрешил вход первому в очереди",
                     slot, -1, 0);
        uint64_t wait = s->now - v->wait_since;
        s->entrance_wait_sum += wait;
        if (wait > s->entrance_wait_max)
            s->entrance_wait_max = wait;
        ++s->entered;
        ++s->gallery;
        if (s->gallery > s->peak_gallery)
            s->peak_gallery = s->gallery;
        v->state = READY;
        output_event(s, "enter", "Посетитель вошёл в галерею", slot, -1, 0);
        choose_or_leave(s, slot);
    }
}

/** @brief Allocate a free visitor slot and model one arrival.
 * @param[in,out] s Model at the arrival's logical timestamp.
 * @return 0 on success, -1 when max_active leaves no free slot.
 * @details Preserves the slot's seen allocation, assigns a new ID, initializes
 * its private choice stream, enqueues the arrival and invokes the watchman.
 */
static int arrive(Simulation *s) {
    int slot;
    for (slot = 0; slot < s->slots; ++slot)
        if (s->visitors[slot].state == FREE)
            break;
    if (slot == s->slots) {
        fprintf(
            stderr,
            "Исчерпан --max-active; увеличьте лимит или интервал прибытия.\n");
        output_event(s, "limit",
                     "Достигнут лимит активных записей; день прерван с ошибкой",
                     -1, -1, 0);
        return -1;
    }
    Visitor *v = &s->visitors[slot];
    unsigned char *seen = v->seen;
    *v = (Visitor){.state = ENTRANCE,
                   .id = ++s->arrived,
                   .wait_since = s->now,
                   .painting = -1,
                   .next = -1,
                   .seen = seen};
    memset(v->seen, 0, (size_t)s->config->paintings);
    strategy_init(s->config, v);
    ++s->active;
    push(s, &s->entrance, slot);
    output_event(s, "arrival", "Посетитель прибыл к входу", slot, -1, 0);
    if (s->gallery == s->config->capacity || s->entrance.length > 1)
        output_event(s, "entrance_wait",
                     "Галерея заполнена; ожидание разрешения вахтёра", slot, -1,
                     0);
    admit(s);
    return 0;
}

/** @brief Complete one reserved viewing and service the released capacity.
 * @param[in,out] s Model at the visitor's completion timestamp.
 * @param slot Valid VIEWING slot whose finish equals the logical clock.
 * @details Marks the painting seen once. Existing painting waiters start before
 * the finishing visitor chooses again; departures then permit entrance
 * admission.
 */
static void finish_view(Simulation *s, int slot) {
    Visitor *v = &s->visitors[slot];
    int painting = v->painting;
    --s->paintings[painting].viewers;
    ++s->paintings[painting].finished;
    ++s->views_finished;
    v->seen[painting] = 1;
    ++v->viewed;
    v->state = READY;
    v->painting = -1;
    output_event(s, "view_end",
                 "Осмотр окончен; картина засчитана, место освобождено", slot,
                 painting, 0);
    start_waiters(s, painting);
    choose_or_leave(s, slot);
    admit(s);
}

/** @brief Unlink a known visitor from its FIFO during cancellation.
 * @param[in,out] s Model holding visitor links.
 * @param[in,out] q FIFO containing slot.
 * @param slot Valid visitor slot known to be a member of q.
 * @pre The queue is consistent and contains slot.
 */
static void remove_waiter(Simulation *s, Queue *q, int slot) {
    int previous = -1;
    for (int i = q->head; i != slot; i = s->visitors[i].next)
        previous = i;
    if (previous >= 0)
        s->visitors[previous].next = s->visitors[slot].next;
    else
        q->head = s->visitors[slot].next;
    if (q->tail == slot)
        q->tail = previous;
    s->visitors[slot].next = -1;
    --q->length;
}

/** @brief Cancel every active visit and release its queue/place ownership.
 * @param[in,out] s Model being shut down.
 * @details Each cancellation updates occupancy and queue links before its event
 * snapshot. Incomplete viewings are not counted as finished.
 */
static void cancel_active(Simulation *s) {
    for (int i = 0; i < s->slots; ++i) {
        Visitor *v = &s->visitors[i];
        if (v->state == FREE)
            continue;
        int painting = (v->state == VIEWING || v->state == PAINTING_WAIT)
                           ? v->painting
                           : -1;
        if (v->state == ENTRANCE)
            remove_waiter(s, &s->entrance, i);
        if (v->state == PAINTING_WAIT)
            remove_waiter(s, &s->paintings[painting].queue, i);
        if (v->state == VIEWING)
            --s->paintings[painting].viewers;
        if (v->state != ENTRANCE)
            --s->gallery;
        --s->active;
        ++s->cancelled;
        output_event(s, "cancel",
                     "Посещение прервано; занятые места освобождены", i,
                     painting, 0);
        v->state = FREE;
    }
}

/* Stable tie order uses visitor IDs, even after record slots are reused. */
/** @brief Find the earliest viewing completion with stable ID tie ordering.
 * @param s Model to scan without changing records.
 * @param[out] time Earliest completion timestamp, or UINT64_MAX if none.
 * @return Selected visitor slot, or -1 if no visitor is VIEWING.
 * @details Visitor IDs, rather than reused slot positions, break ties.
 */
static int next_completion(const Simulation *s, uint64_t *time) {
    uint64_t earliest = UINT64_MAX;
    int slot = -1;
    for (int i = 0; i < s->slots; ++i) {
        const Visitor *v = &s->visitors[i];
        if (v->state == VIEWING &&
            (v->finish < earliest ||
             (v->finish == earliest &&
              (slot < 0 || v->id < s->visitors[slot].id)))) {
            earliest = v->finish;
            slot = i;
        }
    }
    *time = earliest;
    return slot;
}

/** @details Allocates model records, dispatches completion/arrival events,
 * checks invariants, captures censored waits, cancels active records and
 * releases resources on every exit path. Completion wins equal-time arrival
 * ties.
 */
int simulate(const Config *config) {
    Simulation s = {.config = config, .log_fd = -1, .entrance = empty_queue()};
    s.slots = (int)(config->unlimited || config->visitors > config->max_active
                        ? config->max_active
                        : config->visitors);
    if (s.slots == 0)
        s.slots = 1;
    s.visitors = calloc((size_t)s.slots, sizeof(*s.visitors));
    s.membership = calloc((size_t)s.slots, 1);
    s.paintings = calloc((size_t)config->paintings, sizeof(*s.paintings));
    int result = 1;
    if (!s.visitors || !s.membership || !s.paintings) {
        perror("calloc");
        goto cleanup;
    }
    for (int i = 0; i < s.slots; ++i) {
        s.visitors[i].seen = calloc((size_t)config->paintings, 1);
        if (!s.visitors[i].seen) {
            perror("calloc visitor");
            goto cleanup;
        }
    }
    for (uint64_t p = 0; p < config->paintings; ++p)
        s.paintings[p].queue = empty_queue();
    if (output_open(&s))
        goto cleanup;
    output_config(&s);
    uint64_t next_arrival = config->unlimited || config->visitors
                                ? arrival_interval(config, 1)
                                : UINT64_MAX;
    int failed = 0;
    if (next_arrival == UINT64_MAX)
        output_event(&s, "close", "Новые посетители больше не прибывают", -1,
                     -1, 0);
    while (!stop_signal && !s.io_failed &&
           (next_arrival != UINT64_MAX || s.active)) {
        uint64_t earliest;
        int slot = next_completion(&s, &earliest);
        /* Tie rule: completion precedes arrival, then smaller visitor ID. */
        if (slot >= 0 && earliest <= next_arrival) {
            advance(&s, earliest);
            finish_view(&s, slot);
        } else if (next_arrival != UINT64_MAX) {
            advance(&s, next_arrival);
            if (arrive(&s)) {
                failed = 1;
                break;
            }
            if (!config->unlimited && s.arrived == config->visitors) {
                next_arrival = UINT64_MAX;
                output_event(&s, "close",
                             "Новые прибытия закрыты; очередь будет обслужена",
                             -1, -1, 0);
            } else {
                uint64_t interval = arrival_interval(config, s.arrived + 1);
                if (s.now >= UINT64_MAX - interval - config->view_max ||
                    s.arrived == UINT64_MAX) {
                    fprintf(stderr, "Исчерпан диапазон времени/ID модели.\n");
                    failed = 1;
                    break;
                }
                next_arrival = s.now + interval;
            }
        } else {
            fprintf(stderr, "Нет события, способного продолжить модель.\n");
            failed = 1;
            break;
        }
        if (!simulation_check(&s)) {
            fprintf(stderr, "Нарушен инвариант модели на t=%" PRIu64 ".\n",
                    s.now);
            failed = 1;
            break;
        }
    }
    uint64_t pending_entrance = 0, pending_painting = 0;
    for (int i = 0; i < s.slots; ++i) {
        Visitor *v = &s.visitors[i];
        if (v->state == ENTRANCE)
            pending_entrance += s.now - v->wait_since;
        if (v->state == PAINTING_WAIT)
            pending_painting += s.now - v->wait_since;
    }
    if (s.active)
        cancel_active(&s);
    if (!simulation_check(&s)) {
        fprintf(stderr, "Некорректное освобождение модели.\n");
        failed = 1;
    }
    const char *status = failed || s.io_failed ? "failed"
                         : stop_signal         ? "interrupted"
                                               : "completed";
    output_summary(&s, status, pending_entrance, pending_painting);
    result = failed || s.io_failed ? 1 : stop_signal ? 128 + stop_signal : 0;
cleanup:
    if (output_close(&s))
        result = 1;
    if (s.visitors)
        for (int i = 0; i < s.slots; ++i)
            free(s.visitors[i].seen);
    free(s.visitors);
    free(s.membership);
    free(s.paintings);
    return result;
}
