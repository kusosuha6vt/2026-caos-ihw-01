/** @file gallery.c @brief Поведение вахтёра и посетителей; переходы под общим мьютексом. */
#include "simulation_internal.h"
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/** @brief Выждать время осмотра или паузы отображения с обработкой прерываний.
 * @param milliseconds Длительность; мьютекс модели не должен удерживаться.
 */
static void pause_ms(unsigned milliseconds) {
    struct timespec remaining = {(time_t)(milliseconds / MILLISECONDS_PER_SECOND),
                                 (long)(milliseconds % MILLISECONDS_PER_SECOND) *
                                     NANOSECONDS_PER_MILLISECOND};
    while (nanosleep(&remaining, &remaining) != 0) {
        if (errno != EINTR)
            sync_check(errno, "nanosleep");
    }
}

/** @brief Извлечь первого посетителя из очереди. @param g Модель с захваченным мьютексом.
 * @return Извлечённый посетитель; очередь должна быть непустой.
 */
static Visitor *dequeue_locked(Gallery *g) {
    Visitor *visitor = g->entrance.head;
    assert(visitor != NULL && g->entrance.queued > 0);
    g->entrance.head = visitor->next;
    if (g->entrance.head == NULL)
        g->entrance.tail = NULL;
    --g->entrance.queued;
    visitor->next = NULL;
    return visitor;
}

/** @brief Отметить отмену ещё не допущенного посетителя. @param g Модель с захваченным мьютексом.
 * @param visitor Посетитель, не занимающий места в галерее.
 */
static void cancel_visitor_locked(Gallery *g, Visitor *visitor) {
    visitor->state = CANCELLED;
    ++g->stats.cancelled;
    record_event_locked(g, "cancel", "Ожидание входа отменено", visitor->id, 0, 0);
}

/** @brief Выдать разрешения ожидающим посетителям либо отменить их очередь.
 * @param g Модель. @pre Удерживается мьютекс модели; вызывается только вахтёром.
 */
static void serve_queue_locked(Gallery *g) {
    while (g->entrance.head != NULL &&
           (g->lifecycle.stopping || g->entrance.occupied < g->config->capacity)) {
        Visitor *visitor = dequeue_locked(g);
        if (g->lifecycle.stopping)
            cancel_visitor_locked(g, visitor);
        else {
            visitor->state = GRANTED;
            ++g->entrance.occupied;
            ++g->stats.admitted;
            if (g->entrance.occupied > g->stats.maximum)
                g->stats.maximum = g->entrance.occupied;
            assert(g->entrance.occupied <= g->config->capacity);
            record_event_locked(g, "grant", "Вахтёр разрешил вход", visitor->id, 0, 0);
        }
        permit_post(&visitor->admission);
    }
}

/** @brief Поток вахтёра: обслуживание входа и проверка завершения рабочего дня.
 * @param argument Указатель на модель. @return NULL.
 */
void *warden_main(void *argument) {
    Gallery *g = argument;
    for (;;) {
        permit_wait(&g->sync.notifications);
        lock(g);
        serve_queue_locked(g);
        bool done = g->lifecycle.closed && g->entrance.pending == 0 && g->entrance.head == NULL &&
                    g->entrance.occupied == 0;
        if (done)
            record_event_locked(g, "warden_done", "Вахтёр завершил работу", 0, 0, 0);
        unlock(g);
        if (done)
            return NULL;
    }
}

/** @brief Осмотреть одну выбранную картину, ожидая её разрешение вне мьютекса модели.
 * @param visitor Посетитель. @param index Номер картины с нуля.
 */
static void view_painting(Visitor *visitor, unsigned index) {
    Gallery *g = visitor->gallery;
    const Config *c = g->config;
    Painting *painting = &g->storage.pictures[index];
    unsigned duration = sample(&visitor->random, c->view_min_ms, c->view_max_ms);
    lock(g);
    record_event_locked(g, "choose", "Посетитель выбрал картину", visitor->id, index + 1, 0);
    bool available = permit_try(&painting->places);
    if (!available)
        record_event_locked(g,
                            "wait_painting",
                            "Ожидание места возле картины",
                            visitor->id,
                            index + 1,
                            0);
    unlock(g);
    if (!available)
        permit_wait(&painting->places);
    lock(g);
    ++painting->viewers;
    if (painting->viewers > painting->maximum)
        painting->maximum = painting->viewers;
    assert(painting->viewers <= c->painting_capacity);
    record_event_locked(g, "view_begin", "Начало осмотра", visitor->id, index + 1, duration);
    unlock(g);
    pause_ms(duration);
    lock(g);
    assert(painting->viewers > 0);
    --painting->viewers;
    ++g->stats.views;
    record_event_locked(g, "view_end", "Окончание осмотра", visitor->id, index + 1, 0);
    permit_post(&painting->places);
    unlock(g);
    pause_ms(c->display_delay_ms);
}

/** @brief Осмотреть все картины собственного маршрута.
 * @param visitor Допущенный посетитель.
 */
static void view_route(Visitor *visitor) {
    for (unsigned step = 0; step < visitor->gallery->config->paintings; ++step)
        view_painting(visitor, visitor->route[step]);
}

/** @brief Зарегистрировать прибытие и поставить посетителя в очередь либо отменить ожидание.
 * @param visitor Посетитель. @return true при постановке в очередь.
 * @pre Удерживается мьютекс модели.
 */
static bool register_arrival_locked(Visitor *visitor) {
    Gallery *g = visitor->gallery;
    assert(g->entrance.pending > 0);
    --g->entrance.pending;
    ++g->stats.arrived;
    record_event_locked(g, "arrive", "Посетитель прибыл", visitor->id, 0, visitor->arrival_delay);
    if (g->lifecycle.stopping) {
        cancel_visitor_locked(g, visitor);
        return false;
    }
    visitor->state = QUEUED;
    if (g->entrance.tail != NULL)
        g->entrance.tail->next = visitor;
    else
        g->entrance.head = visitor;
    g->entrance.tail = visitor;
    ++g->entrance.queued;
    record_event_locked(g, "queue", "Посетитель ожидает разрешения вахтёра", visitor->id, 0, 0);
    if (g->entrance.occupied == g->config->capacity)
        record_event_locked(g,
                            "wait_entrance",
                            "Галерея заполнена, ожидание у входа",
                            visitor->id,
                            0,
                            0);
    return true;
}

/** @brief Зарегистрировать прибытие и дождаться персонального допуска.
 * @param visitor Посетитель. @return true после входа, false при отмене.
 */
static bool enter_gallery(Visitor *visitor) {
    Gallery *g = visitor->gallery;
    lock(g);
    bool queued = register_arrival_locked(visitor);
    permit_post(&g->sync.notifications);
    unlock(g);
    if (!queued)
        return false;
    permit_wait(&visitor->admission);
    lock(g);
    bool admitted = visitor->state != CANCELLED;
    if (admitted) {
        assert(visitor->state == GRANTED);
        visitor->state = INSIDE;
        record_event_locked(g, "enter", "Посетитель вошёл в галерею", visitor->id, 0, 0);
    }
    unlock(g);
    return admitted;
}

/** @brief Завершить маршрут и освободить место галереи.
 * @param visitor Посетитель после осмотра всех картин.
 */
static void leave_gallery(Visitor *visitor) {
    Gallery *g = visitor->gallery;
    lock(g);
    assert(g->entrance.occupied > 0);
    visitor->state = FINISHED;
    --g->entrance.occupied;
    ++g->stats.completed;
    record_event_locked(g, "exit", "Посетитель вышел из галереи", visitor->id, 0, 0);
    permit_post(&g->sync.notifications);
    unlock(g);
}

/** @brief Поток посетителя: вход, осмотр маршрута и выход.
 * @param argument Указатель на Visitor. @return NULL.
 */
void *visitor_main(void *argument) {
    Visitor *visitor = argument;
    if (enter_gallery(visitor)) {
        view_route(visitor);
        leave_gallery(visitor);
    }
    return NULL;
}
