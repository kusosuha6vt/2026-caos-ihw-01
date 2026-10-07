/** @file output.c
 * @brief Descriptor-based output, bounded journal locking and statistics.
 */

#include "output.h"

#include "internal/model.h"
#include "signals.h"
#include "strategy.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/** @details Checks existing inode aliases before truncating the event log.
 * A log path of '-' disables the descriptor.
 */
int output_open(Simulation *simulation) {
    const Config *c = simulation->config;
    if (strcmp(c->log_path, "-") != 0) {
        /* Detect existing hard/symbolic aliases before O_TRUNC can damage a
         * journal. */
        struct stat log_stat, journal_stat;
        if (strcmp(c->journal_path, "-") != 0 &&
            !stat(c->log_path, &log_stat) &&
            !stat(c->journal_path, &journal_stat) &&
            log_stat.st_dev == journal_stat.st_dev &&
            log_stat.st_ino == journal_stat.st_ino) {
            fprintf(stderr, "Лог и журнал ссылаются на один файл.\n");
            return -1;
        }
        simulation->log_fd =
            open(c->log_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (simulation->log_fd < 0) {
            perror(c->log_path);
            return -1;
        }
    }
    return 0;
}

/** @details A successful close resets log_fd to -1; failure is reported
 * to the caller so shutdown can return a nonzero exit status.
 */
int output_close(Simulation *simulation) {
    if (simulation->log_fd >= 0 && close(simulation->log_fd)) {
        perror("close log");
        return -1;
    }
    simulation->log_fd = -1;
    return 0;
}

/** @brief Write a complete byte buffer through a POSIX descriptor.
 * @param fd Writable descriptor.
 * @param text Buffer containing at least length bytes.
 * @param length Number of bytes to write.
 * @return 0 on complete delivery, -1 on failure or interrupted retry.
 * @details Handles short writes and EINTR; a zero-byte write is an EIO failure.
 */
static int write_all(int fd, const char *text, size_t length) {
    while (length) {
        ssize_t count = write(fd, text, length);
        if (count < 0 && errno == EINTR) {
            if (stop_signal)
                return -1;
            continue;
        }
        if (count <= 0) {
            if (!count)
                errno = EIO;
            return -1;
        }
        text += count;
        length -= (size_t)count;
    }
    return 0;
}

/** @brief Write an event/summary to the enabled log and console destinations.
 * @param[in,out] s Model whose io_failed flag records output errors.
 * @param json Complete JSONL record including its trailing newline.
 * @param human Complete human-readable record.
 * @param summary Nonzero to emit the final record even in quiet mode.
 * @details Quiet/JSON modes select json; the first delivery error is reported.
 */
static void output(Simulation *s, const char *json, const char *human,
                   int summary) {
    if ((s->log_fd >= 0 && write_all(s->log_fd, json, strlen(json))) ||
        ((!s->config->quiet || summary) &&
         write_all(
             STDOUT_FILENO,
             (s->config->json || s->config->quiet) ? json : human,
             strlen((s->config->json || s->config->quiet) ? json : human)))) {
        if (!s->io_failed)
            perror("Ошибка записи вывода");
        s->io_failed = 1;
    }
}

/** @brief Apply presentation delay without changing the logical clock.
 * @param c Configuration supplying delay_ms.
 * @details Retries interrupted sleep only while no stop signal is pending.
 */
static void pace(const Config *c) {
    struct timespec delay = {.tv_sec = (time_t)(c->delay_ms / 1000),
                             .tv_nsec = (long)((c->delay_ms % 1000) * 1000000)};
    while (c->delay_ms && !stop_signal && nanosleep(&delay, &delay) < 0) {
        if (errno != EINTR)
            break;
    }
}

/** @details Formats a snapshot from current records, increments seq, writes
 * synchronously and then applies presentation pacing.
 */
void output_event(Simulation *simulation, const char *kind, const char *message,
                  int slot, int painting, uint64_t duration) {
    Visitor *v = slot >= 0 ? &simulation->visitors[slot] : NULL;
    Painting *p = painting >= 0 ? &simulation->paintings[painting] : NULL;
    char json[2048], human[2048];
    snprintf(json, sizeof(json),
             "{\"seq\":%" PRIu64 ",\"time_ms\":%" PRIu64 ",\"event\":\"%s\","
             "\"visitor\":%" PRIu64 ",\"painting\":%d,\"gallery\":%" PRIu64 ","
             "\"viewers\":%" PRIu64 ",\"entrance_queue\":%" PRIu64 ","
             "\"painting_queue\":%" PRIu64 ",\"duration_ms\":%" PRIu64 ","
             "\"viewed\":%" PRIu64 ",\"message\":\"%s\"}\n",
             ++simulation->seq, simulation->now, kind, v ? v->id : 0,
             painting + 1, simulation->gallery, p ? p->viewers : 0,
             simulation->entrance.length, p ? p->queue.length : 0, duration,
             v ? v->viewed : 0, message);
    snprintf(human, sizeof(human),
             "[%6" PRIu64 " мс] %-12s | посетитель=%" PRIu64 " картина=%d "
             "галерея=%" PRIu64 "/%" PRIu64 " у картины=%" PRIu64 " | %s\n",
             simulation->now, kind, v ? v->id : 0, painting + 1,
             simulation->gallery, simulation->config->capacity,
             p ? p->viewers : 0, message);
    output(simulation, json, human, 0);
    pace(simulation->config);
}

/** @details Writes the configuration with sequence/time zero before events.
 */
void output_config(Simulation *simulation) {
    const Config *c = simulation->config;
    char json[2048], human[1024];
    snprintf(
        json, sizeof(json),
        "{\"seq\":0,\"time_ms\":0,\"event\":\"config\",\"capacity\":%" PRIu64
        ",\"paintings\":%" PRIu64 ",\"painting_capacity\":%" PRIu64
        ",\"visitors\":%" PRIu64 ",\"unlimited\":%d,\"arrival_min\":%" PRIu64
        ",\"arrival_max\":%" PRIu64 ",\"view_min\":%" PRIu64
        ",\"view_max\":%" PRIu64 ",\"seed\":%" PRIu64
        ",\"strategy\":\"%s\",\"delay_ms\":%" PRIu64 ",\"max_active\":%" PRIu64
        "}\n",
        c->capacity, c->paintings, c->painting_capacity, c->visitors,
        c->unlimited, c->arrival_min, c->arrival_max, c->view_min, c->view_max,
        c->seed, strategy_name(c->strategy), c->delay_ms, c->max_active);
    snprintf(human, sizeof(human),
             "Галерея: вместимость=%" PRIu64 ", картин=%" PRIu64
             ", мест у картины=%" PRIu64 "; посетителей=%" PRIu64
             " (%s), стратегия=%s, seed=%" PRIu64 ".\n"
             "Время модели не зависит от задержки отображения.\n",
             c->capacity, c->paintings, c->painting_capacity, c->visitors,
             c->unlimited ? "неограниченный режим" : "конечный режим",
             strategy_name(c->strategy), c->seed);
    output(simulation, json, human, 0);
}

/** @brief Append a CSV header/row under a bounded POSIX advisory lock.
 * @param s Model supplying configuration and accumulated statistics.
 * @param status Internal run status string.
 * @param pending_entrance Censored entrance-wait sum in logical milliseconds.
 * @param pending_painting Censored painting-wait sum in logical milliseconds.
 * @param gallery_util Gallery occupancy fraction integrated over the day.
 * @param painting_util Aggregate painting occupancy fraction.
 * @return 0 on success/disabled journal, -1 on open, lock, write or close
 * failure.
 * @details Holds one lock across header and row writes, and limits lock retries
 * so an external holder cannot trap shutdown indefinitely.
 */
static int journal(Simulation *s, const char *status, uint64_t pending_entrance,
                   uint64_t pending_painting, double gallery_util,
                   double painting_util) {
    if (!strcmp(s->config->journal_path, "-"))
        return 0;
    int fd = open(s->config->journal_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0)
        return -1;
    int result = -1;
    struct flock lock = {
        .l_type = F_WRLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = 0};
    /* A stuck external journal writer must not make shutdown wait forever. */
    for (unsigned attempt = 0; fcntl(fd, F_SETLK, &lock) < 0; ++attempt) {
        if ((errno != EACCES && errno != EAGAIN && errno != EINTR) ||
            stop_signal || attempt >= 999)
            goto done;
        struct timespec pause = {.tv_nsec = 1000000};
        while (nanosleep(&pause, &pause) < 0)
            if (errno != EINTR || stop_signal)
                goto done;
    }
    struct stat st;
    if (fstat(fd, &st))
        goto done;
    static const char header[] =
        "strategy,seed,capacity,paintings,painting_capacity,visitors,"
        "unlimited,arrival_min,arrival_max,"
        "view_min,view_max,status,time_ms,arrived,entered,completed,"
        "cancelled,views_started,views_finished,"
        "mean_entrance_wait_ms,max_entrance_wait_ms,mean_painting_wait_"
        "ms,max_painting_wait_ms,"
        "pending_entrance_wait_ms,pending_painting_wait_ms,peak_"
        "gallery,gallery_utilization,painting_utilization\n";
    if (!st.st_size && write_all(fd, header, sizeof(header) - 1))
        goto done;
    char row[2048];
    const Config *c = s->config;
    snprintf(row, sizeof(row),
             "%s,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
             ",%d,"
             "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%s,%" PRIu64 ","
             "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
             ",%" PRIu64 ","
             "%.6f,%" PRIu64 ",%.6f,%" PRIu64 ",%" PRIu64 ",%" PRIu64
             ",%" PRIu64 ",%.9f,%.9f\n",
             strategy_name(c->strategy), c->seed, c->capacity, c->paintings,
             c->painting_capacity, c->visitors, c->unlimited, c->arrival_min,
             c->arrival_max, c->view_min, c->view_max, status, s->now,
             s->arrived, s->entered, s->completed, s->cancelled,
             s->views_started, s->views_finished,
             s->entered ? (double)s->entrance_wait_sum / (double)s->entered : 0,
             s->entrance_wait_max,
             s->views_started
                 ? (double)s->painting_wait_sum / (double)s->views_started
                 : 0,
             s->painting_wait_max, pending_entrance, pending_painting,
             s->peak_gallery, gallery_util, painting_util);
    result = write_all(fd, row, strlen(row));
done:
    if (close(fd))
        result = -1;
    return result;
}

/** @details Aggregates painting statistics, appends the journal first, then
 * emits painting statistics and summary. Output failure updates the model
 * status.
 */
void output_summary(Simulation *simulation, const char *status,
                    uint64_t pending_entrance, uint64_t pending_painting) {
    long double total_painting_area = 0;
    for (uint64_t p = 0; p < simulation->config->paintings; ++p)
        total_painting_area += simulation->paintings[p].area;
    double gu = simulation->now
                    ? (double)(simulation->gallery_area /
                               ((long double)simulation->now *
                                (long double)simulation->config->capacity))
                    : 0;
    double pu =
        simulation->now
            ? (double)(total_painting_area /
                       ((long double)simulation->now *
                        (long double)simulation->config->paintings *
                        (long double)simulation->config->painting_capacity))
            : 0;
    if (journal(simulation, status, pending_entrance, pending_painting, gu,
                pu)) {
        perror("Ошибка записи журнала результатов");
        simulation->io_failed = 1;
        status = "failed";
    }
    for (uint64_t p = 0; p < simulation->config->paintings; ++p) {
        Painting *painting = &simulation->paintings[p];
        char json[1024], human[1024];
        double util =
            simulation->now
                ? (double)(painting->area /
                           ((long double)simulation->now *
                            (long double)simulation->config->painting_capacity))
                : 0;
        snprintf(json, sizeof(json),
                 "{\"seq\":%" PRIu64 ",\"time_ms\":%" PRIu64
                 ",\"event\":\"painting_stats\","
                 "\"painting\":%" PRIu64 ",\"started\":%" PRIu64
                 ",\"finished\":%" PRIu64 ",\"peak\":%" PRIu64
                 ",\"utilization\":%.9f}\n",
                 ++simulation->seq, simulation->now, p + 1, painting->started,
                 painting->finished, painting->peak, util);
        snprintf(human, sizeof(human),
                 "Картина %" PRIu64 ": осмотров=%" PRIu64 ", пик=%" PRIu64
                 ", использование=%.2f%%\n",
                 p + 1, painting->finished, painting->peak, util * 100);
        output(simulation, json, human, 0);
    }
    if (simulation->io_failed)
        status = "failed";
    char json[2048], human[2048];
    double ew = simulation->entered ? (double)simulation->entrance_wait_sum /
                                          (double)simulation->entered
                                    : 0;
    double pw = simulation->views_started
                    ? (double)simulation->painting_wait_sum /
                          (double)simulation->views_started
                    : 0;
    snprintf(
        json, sizeof(json),
        "{\"seq\":%" PRIu64 ",\"time_ms\":%" PRIu64
        ",\"event\":\"summary\",\"status\":\"%s\","
        "\"arrived\":%" PRIu64 ",\"entered\":%" PRIu64 ",\"completed\":%" PRIu64
        ","
        "\"cancelled\":%" PRIu64 ",\"views_started\":%" PRIu64
        ",\"views_finished\":%" PRIu64 ","
        "\"mean_entrance_wait_ms\":%.6f,\"max_entrance_wait_ms\":%" PRIu64 ","
        "\"mean_painting_wait_ms\":%.6f,\"max_painting_wait_ms\":%" PRIu64 ","
        "\"pending_entrance_wait_ms\":%" PRIu64
        ",\"pending_painting_wait_ms\":%" PRIu64 ","
        "\"peak_gallery\":%" PRIu64
        ",\"gallery_utilization\":%.9f,\"painting_utilization\":%.9f,"
        "\"signal\":%d}\n",
        ++simulation->seq, simulation->now, status, simulation->arrived,
        simulation->entered, simulation->completed, simulation->cancelled,
        simulation->views_started, simulation->views_finished, ew,
        simulation->entrance_wait_max, pw, simulation->painting_wait_max,
        pending_entrance, pending_painting, simulation->peak_gallery, gu, pu,
        (int)stop_signal);
    snprintf(human, sizeof(human),
             "ИТОГ: %s, время=%" PRIu64 " мс; прибыло=%" PRIu64
             ", вошло=%" PRIu64 ", завершило=%" PRIu64 ", прервано=%" PRIu64
             ".\n"
             "Среднее ожидание входа=%.3f мс, картины=%.3f мс; использование "
             "галереи=%.2f%%.\n",
             status, simulation->now, simulation->arrived, simulation->entered,
             simulation->completed, simulation->cancelled, ew, pw, gu * 100);
    output(simulation, json, human, 1);
}
