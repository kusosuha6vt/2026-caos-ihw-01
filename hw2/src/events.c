/** @file events.c @brief Согласованный вывод конфигурации, событий и итогов. */
#include "simulation_internal.h"
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <stdarg.h>
#include <stdio.h>

/** @brief Получить показание часов с проверкой ошибки. @param clock Идентификатор часов. @return
 * Показание часов. */
struct timespec now(clockid_t clock) {
    struct timespec value;
    if (clock_gettime(clock, &value) != 0)
        sync_check(errno, "clock_gettime");
    return value;
}

/** @brief Вычислить прошедшее время в миллисекундах. @param g Модель. @return Прошедшее время по
 * монотонным часам. */
static double elapsed(const Gallery *g) {
    struct timespec current = now(CLOCK_MONOTONIC);
    return (double)(current.tv_sec - g->output.started.tv_sec) * MILLISECONDS_PER_SECOND +
           (double)(current.tv_nsec - g->output.started.tv_nsec) /
               (double)NANOSECONDS_PER_MILLISECOND;
}

/** @brief Записать строку в одно назначение и отключить его при ошибке.
 * @param output Состояние вывода. @param sink Назначение.
 * @param data Строка для записи. @param length Размер строки.
 * @pre Удерживается мьютекс модели либо рабочие потоки ещё не запущены/уже присоединены.
 */
static void write_sink(GalleryOutput *output, OutputSink *sink, const char *data, size_t length) {
    if (!sink->enabled || fd_write_all(sink->fd, data, length) == 0)
        return;
    diagnostic("Cannot write %s: %s", sink->name, strerror(errno));
    sink->enabled = false;
    output->failed = true;
}

/** @brief Сформировать и записать строку JSON под мьютексом модели.
 * @param g Модель. @param format Формат без строк JSON, задаваемых пользователем.
 * @details Неисправные назначения отключаются, остальные продолжают работать. Ошибки
 * дают ненулевой код выхода после завершения всех допущенных посетителей.
 */
static void GALLERY_PRINTF_FORMAT(2, 3) output(Gallery *g, const char *format, ...) {
    char buffer[EVENT_BUFFER_CAPACITY];
    va_list args;
    va_start(args, format);
    int count = vsnprintf(buffer, sizeof(buffer) - 1, format, args);
    va_end(args);
    if (count < 0 || (size_t)count >= sizeof(buffer) - 1)
        sync_check(EOVERFLOW, "event format");
    size_t length = (size_t)count;
    buffer[length++] = '\n';
    write_sink(&g->output, &g->output.console, buffer, length);
    write_sink(&g->output, &g->output.log, buffer, length);
}

/** @brief Зафиксировать наблюдаемый переход состояния; вызывающая сторона удерживает мьютекс
 * модели.
 * @param g Модель. @param name Обозначение события для программной обработки. @param text Описание
 * на русском языке.
 * @param visitor Номер посетителя от 1 или ноль. @param painting Номер картины от 1 или ноль.
 * @param delay Назначенная длительность или интервал, если применимо.
 */
void record_event_locked(Gallery *g,
                         const char *name,
                         const char *text,
                         unsigned visitor,
                         unsigned painting,
                         unsigned delay) {
    output(g,
           "{\"type\":\"event\",\"seq\":%" PRIu64 ",\"time_ms\":%.3f,"
           "\"event\":\"%s\",\"text\":\"%s\",\"visitor\":%u,\"painting\":%u,"
           "\"gallery\":%u,\"viewers\":%u,\"queue\":%u,\"delay_ms\":%u}",
           ++g->output.sequence,
           elapsed(g),
           name,
           text,
           visitor,
           painting,
           g->entrance.occupied,
           painting == 0 ? 0 : g->storage.pictures[painting - 1].viewers,
           g->entrance.queued,
           delay);
}

/** @brief Вывести конфигурацию и название реализации синхронизации до событий. @param g Модель. */
void print_config(Gallery *g) {
    const Config *c = g->config;
    output(g,
           "{\"type\":\"config\",\"backend\":\"%s\",\"capacity\":%u,\"paintings\":%u,"
           "\"painting_capacity\":%u,\"visitors\":%u,\"arrival_min_ms\":%u,\"arrival_max_ms\":%u,"
           "\"view_min_ms\":%u,\"view_max_ms\":%u,\"display_delay_ms\":%u,"
           "\"strategy\":\"%s\",\"seed\":%" PRIu64 "}",
           GALLERY_COND ? "condition" : "semaphore",
           c->capacity,
           c->paintings,
           c->painting_capacity,
           c->visitors,
           c->arrival_min_ms,
           c->arrival_max_ms,
           c->view_min_ms,
           c->view_max_ms,
           c->display_delay_ms,
           c->strategy == STRATEGY_RANDOM ? "random" : "sequential",
           c->seed);
}

/** @brief Вывести максимумы картин и общие итоги. @param g Модель после присоединения рабочих
 * потоков. */
void print_summary(Gallery *g) {
    for (unsigned p = 0; p < g->config->paintings; ++p) {
        assert(g->storage.pictures[p].viewers == 0);
        output(g,
               "{\"type\":\"painting_summary\",\"painting\":%u,\"maximum\":%u,\"viewers\":%u}",
               p + 1,
               g->storage.pictures[p].maximum,
               g->storage.pictures[p].viewers);
    }
    assert(g->entrance.occupied == 0 && g->entrance.queued == 0 && g->entrance.pending == 0);
    const char *reason = g->lifecycle.failed || g->output.failed ? "error"
                         : g->lifecycle.stop_signal              ? "signal"
                                                                 : "completed";
    output(g,
           "{\"type\":\"summary\",\"reason\":\"%s\",\"signal\":%d,\"requested\":%u,"
           "\"created\":%u,\"arrived\":%u,\"admitted\":%u,\"completed\":%u,\"cancelled\":%u,"
           "\"views\":%" PRIu64 ",\"maximum\":%u,\"gallery\":%u,\"queue\":%u,\"time_ms\":%.3f}",
           reason,
           g->lifecycle.stop_signal,
           g->config->visitors,
           g->stats.created,
           g->stats.arrived,
           g->stats.admitted,
           g->stats.completed,
           g->stats.cancelled,
           g->stats.views,
           g->stats.maximum,
           g->entrance.occupied,
           g->entrance.queued,
           elapsed(g));
}

/** @brief Открыть необязательный журнал до запуска модели.
 * @param g Модель с начальными назначениями вывода. @return 0 либо -1.
 */
int initialize_output(Gallery *g) {
    if (g->config->log_path == NULL)
        return 0;
    g->output.log.fd = open_log_file(g->config);
    if (g->output.log.fd < 0) {
        diagnostic("Cannot open log: %s", strerror(errno));
        return -1;
    }
    g->output.log.enabled = true;
    return 0;
}

/** @brief Закрыть принадлежащий модели журнал с сохранением ошибки в коде выхода.
 * @param g Модель без работающих потоков. @param result Предварительный код выхода.
 * @return Итоговый код выхода.
 */
int close_output(Gallery *g, int result) {
    if (g->output.log.fd >= 0 && close(g->output.log.fd) != 0) {
        diagnostic("Cannot close log: %s", strerror(errno));
        return EXIT_FAILURE;
    }
    return result;
}
