/** @file simulation.c @brief Подготовка ресурсов, управление потоками и завершение модели. */
#include "simulation.h"
#include "exit_status.h"
#include "simulation_internal.h"
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/** @brief Ожидать истечения интервала прибытия или запроса остановки.
 * @param g Модель. @param milliseconds Задержка. @return Можно ли продолжать создание посетителей.
 */
static bool arrival_delay(Gallery *g, unsigned milliseconds) {
    struct timespec deadline = now(g->sync.wait_clock);
    deadline.tv_sec += (time_t)(milliseconds / MILLISECONDS_PER_SECOND);
    deadline.tv_nsec +=
        (long)(milliseconds % MILLISECONDS_PER_SECOND) * NANOSECONDS_PER_MILLISECOND;
    if (deadline.tv_nsec >= NANOSECONDS_PER_SECOND) {
        ++deadline.tv_sec;
        deadline.tv_nsec -= NANOSECONDS_PER_SECOND;
    }
    lock(g);
    while (!g->lifecycle.stopping && milliseconds != 0) {
        int error = pthread_cond_timedwait(&g->sync.arrival, &g->sync.mutex, &deadline);
        if (error == ETIMEDOUT)
            break;
        sync_check(error, "arrival wait");
    }
    bool running = !g->lifecycle.stopping;
    unlock(g);
    return running;
}

/** @brief Принимать заблокированные сигналы без небезопасных асинхронных обработчиков.
 * @param argument Указатель на модель. @return NULL после пробуждения с установленным workers_done.
 */
static void *signal_main(void *argument) {
    Gallery *g = argument;
    for (;;) {
        int signal_number;
        sync_check(sigwait(&g->lifecycle.signals, &signal_number), "sigwait");
        lock(g);
        if (g->lifecycle.workers_done) {
            unlock(g);
            return NULL;
        }
        if ((signal_number == SIGINT || signal_number == SIGTERM) && !g->lifecycle.stopping) {
            g->lifecycle.stopping = true;
            g->lifecycle.stop_signal = signal_number;
            record_event_locked(g,
                                "stop",
                                "Получен сигнал: новые входы прекращены",
                                0,
                                0,
                                (unsigned)signal_number);
            sync_check(pthread_cond_broadcast(&g->sync.arrival), "stop arrivals");
            permit_post(&g->sync.notifications);
        }
        unlock(g);
    }
}

/** @brief Освободить все принадлежащие модели массивы и подготовленные разрешения.
 * @param g Модель без работающих потоков.
 */
static void release_storage(Gallery *g) {
    GalleryStorage *storage = &g->storage;
    for (unsigned i = 0; i < storage->initialized_visitors; ++i)
        permit_destroy(&storage->visitors[i].admission);
    for (unsigned i = 0; i < storage->initialized_pictures; ++i)
        permit_destroy(&storage->pictures[i].places);
    free(storage->routes);
    free(storage->visitors);
    free(storage->pictures);
    *storage = (GalleryStorage){0};
}

/** @brief Выделить память участников, проверяя размер массива маршрутов.
 * @param g Модель. @return 0 при успехе, -1 при ошибке.
 */
static int allocate_storage(Gallery *g) {
    const Config *c = g->config;
    GalleryStorage *storage = &g->storage;
    size_t count = c->visitors == 0 ? 1 : c->visitors;
    if (c->paintings != 0 && count > SIZE_MAX / c->paintings) {
        diagnostic("Route allocation size overflow");
        return -1;
    }
    storage->pictures = calloc(c->paintings, sizeof(*storage->pictures));
    storage->visitors = calloc(count, sizeof(*storage->visitors));
    storage->routes = calloc(count * c->paintings, sizeof(*storage->routes));
    return storage->pictures != NULL && storage->visitors != NULL && storage->routes != NULL ? 0
                                                                                             : -1;
}

/** @brief Подготовить разрешения всех картин с учётом частичной инициализации.
 * @param g Модель. @return 0 при успехе, -1 при ошибке.
 */
static int initialize_paintings(Gallery *g) {
    GalleryStorage *storage = &g->storage;
    for (; storage->initialized_pictures < g->config->paintings; ++storage->initialized_pictures) {
        Painting *painting = &storage->pictures[storage->initialized_pictures];
        int error = permit_init(&painting->places, g->config->painting_capacity);
        if (error != 0) {
            diagnostic("Cannot initialize painting permit: %s", strerror(error));
            return -1;
        }
    }
    return 0;
}

/** @brief Заполнить маршрут посетителя и перемешать его для случайной стратегии.
 * @param visitor Посетитель с выделенным маршрутом и начальным состоянием генератора.
 * @param paintings Число картин. @param strategy Стратегия порядка осмотра.
 */
static void prepare_route(Visitor *visitor, unsigned paintings, PaintingStrategy strategy) {
    for (unsigned p = 0; p < paintings; ++p)
        visitor->route[p] = p;
    if (strategy != STRATEGY_RANDOM)
        return;
    for (unsigned p = paintings; p > 1; --p) {
        unsigned other = (unsigned)random_below(&visitor->random, p);
        unsigned temporary = visitor->route[p - 1];
        visitor->route[p - 1] = visitor->route[other];
        visitor->route[other] = temporary;
    }
}

/** @brief Подготовить посетителей и личные разрешения до запуска потоков.
 * @param g Модель. @return 0 при успехе, -1 при ошибке.
 */
static int initialize_visitors(Gallery *g) {
    GalleryStorage *storage = &g->storage;
    const Config *c = g->config;
    for (; storage->initialized_visitors < c->visitors; ++storage->initialized_visitors) {
        unsigned index = storage->initialized_visitors;
        Visitor *visitor = &storage->visitors[index];
        int error = permit_init(&visitor->admission, 0);
        if (error != 0) {
            diagnostic("Cannot initialize admission permit: %s", strerror(error));
            return -1;
        }
        visitor->gallery = g;
        visitor->id = index + 1;
        visitor->route = storage->routes + (size_t)index * c->paintings;
        visitor->random = random_derive_seed(c->seed, visitor->id);
        prepare_route(visitor, c->paintings, c->strategy);
    }
    return 0;
}

/** @brief Подготовить память и разрешения; при ошибке очистка остаётся вызывающей стороне.
 * @param g Модель. @return 0 при успехе, -1 при ошибке.
 */
static int prepare_storage(Gallery *g) {
    if (allocate_storage(g) == 0 && initialize_paintings(g) == 0 && initialize_visitors(g) == 0)
        return 0;
    diagnostic("Cannot prepare simulation resources");
    return -1;
}

/** @brief Создать один поток посетителя; тестовая сборка допускает внедрение отказа.
 * @param visitor Подготовленная запись посетителя. @return Код pthread_create.
 */
static int create_visitor_thread(Visitor *visitor) {
#ifdef GALLERY_TESTING
    const char *fail_after = getenv("GALLERY_FAIL_AFTER");
    if (fail_after != NULL && strtoul(fail_after, NULL, 10) == visitor->id - 1)
        return EAGAIN;
#endif
    return pthread_create(&visitor->thread, NULL, visitor_main, visitor);
}

/** @brief Учесть ожидаемую регистрацию и запустить одного посетителя.
 * @param g Модель. @param index Номер записи. @param delay Назначенный интервал прибытия.
 * @return true при успешном создании, false при остановке или ошибке.
 */
static bool start_visitor(Gallery *g, unsigned index, unsigned delay) {
    Visitor *visitor = &g->storage.visitors[index];
    lock(g);
    if (g->lifecycle.stopping) {
        unlock(g);
        return false;
    }
    ++g->entrance.pending;
    visitor->arrival_delay = delay;
    unlock(g);
    int error = create_visitor_thread(visitor);
    lock(g);
    if (error != 0) {
        --g->entrance.pending;
        g->lifecycle.failed = true;
        diagnostic("Cannot create visitor %u: %s", index + 1, strerror(error));
        record_event_locked(g, "error", "Ошибка создания посетителя", index + 1, 0, 0);
    } else {
        ++g->stats.created;
    }
    unlock(g);
    return error == 0;
}

/** @brief Создавать посетителей до исчерпания числа прибытий или запроса остановки.
 * @param g Модель с запущенными служебными потоками.
 */
static void generate(Gallery *g) {
    const Config *c = g->config;
    uint64_t random = c->seed;
    for (unsigned i = 0; i < c->visitors; ++i) {
        unsigned delay = sample(&random, c->arrival_min_ms, c->arrival_max_ms);
        if (!arrival_delay(g, delay) || !start_visitor(g, i, delay))
            break;
    }
    lock(g);
    g->lifecycle.closed = true;
    record_event_locked(g,
                        "entrance_closed",
                        "Источник прибытий закрыт, очередь обслуживается",
                        0,
                        0,
                        0);
    permit_post(&g->sync.notifications);
    unlock(g);
}

/** @brief Подготовить обработку сигналов до создания потоков.
 * @param g Модель. @return 0 при успехе, -1 при ошибке sigaction.
 */
static int initialize_signals(Gallery *g) {
    struct sigaction ignore = {.sa_handler = SIG_IGN};
    sigemptyset(&ignore.sa_mask);
    if (sigaction(SIGPIPE, &ignore, NULL) != 0) {
        diagnostic("Cannot ignore SIGPIPE");
        return -1;
    }
    sigemptyset(&g->lifecycle.signals);
    sigaddset(&g->lifecycle.signals, SIGINT);
    sigaddset(&g->lifecycle.signals, SIGTERM);
    sigaddset(&g->lifecycle.signals, SIGUSR1);
    sync_check(pthread_sigmask(SIG_BLOCK, &g->lifecycle.signals, NULL), "block signals");
    return 0;
}

/** @brief Подготовить мьютекс, условную переменную и уведомления вахтёра.
 * @param g Модель. @return 0 при успехе; -1 с полной очисткой при ошибке разрешения.
 */
static int initialize_sync(Gallery *g) {
    GallerySync *sync = &g->sync;
    sync_check(pthread_mutex_init(&sync->mutex, NULL), "model mutex init");
    pthread_condattr_t attributes;
    sync_check(pthread_condattr_init(&attributes), "arrival attributes");
#ifdef __APPLE__
    sync->wait_clock = CLOCK_REALTIME;
#else
    sync->wait_clock = CLOCK_MONOTONIC;
    sync_check(pthread_condattr_setclock(&attributes, sync->wait_clock), "arrival clock");
#endif
    sync_check(pthread_cond_init(&sync->arrival, &attributes), "arrival condition init");
    sync_check(pthread_condattr_destroy(&attributes), "arrival attributes destroy");
    int error = permit_init(&sync->notifications, 0);
    if (error == 0)
        return 0;
    diagnostic("Cannot initialize warden notifications: %s", strerror(error));
    sync_check(pthread_cond_destroy(&sync->arrival), "arrival condition destroy");
    sync_check(pthread_mutex_destroy(&sync->mutex), "model mutex destroy");
    return -1;
}

/** @brief Уничтожить полностью подготовленные примитивы после завершения всех участников.
 * @param g Модель без работающих потоков.
 */
static void destroy_sync(Gallery *g) {
    permit_destroy(&g->sync.notifications);
    sync_check(pthread_cond_destroy(&g->sync.arrival), "arrival condition destroy");
    sync_check(pthread_mutex_destroy(&g->sync.mutex), "model mutex destroy");
}

/** @brief Запустить служебные потоки; при частичном отказе присоединить запущенный поток.
 * @param g Модель. @param threads Дескрипторы потоков. @return 0 либо -1.
 */
static int start_workers(Gallery *g, WorkerThreads *threads) {
    int error = pthread_create(&threads->warden, NULL, warden_main, g);
    if (error != 0) {
        diagnostic("Cannot create warden: %s", strerror(error));
        return -1;
    }
    error = pthread_create(&threads->signals, NULL, signal_main, g);
    if (error == 0)
        return 0;
    diagnostic("Cannot create signal thread: %s", strerror(error));
    lock(g);
    g->lifecycle.closed = true;
    permit_post(&g->sync.notifications);
    unlock(g);
    sync_check(pthread_join(threads->warden, NULL), "join warden after startup error");
    return -1;
}

/** @brief Присоединить посетителей, вахтёра и затем сигнальный поток.
 * @param g Модель после окончания генерации. @param threads Запущенные служебные потоки.
 */
static void finish_workers(Gallery *g, const WorkerThreads *threads) {
    for (unsigned i = 0; i < g->stats.created; ++i)
        sync_check(pthread_join(g->storage.visitors[i].thread, NULL), "join visitor");
    sync_check(pthread_join(threads->warden, NULL), "join warden");
    lock(g);
    g->lifecycle.workers_done = true;
    unlock(g);
    sync_check(pthread_kill(threads->signals, SIGUSR1), "wake signal thread");
    sync_check(pthread_join(threads->signals, NULL), "join signal thread");
}

/** @brief Выполнить рабочий день после подготовки всех ресурсов.
 * @param g Модель. @return 0 после присоединения участников, -1 при отказе запуска.
 */
static int run_workers(Gallery *g) {
    WorkerThreads threads;
    if (start_workers(g, &threads) != 0)
        return -1;
    generate(g);
    finish_workers(g, &threads);
    return 0;
}

/** @brief Выбрать код завершения по ошибкам и полученному сигналу.
 * @param g Модель после присоединения участников. @return Код завершения приложения.
 */
static int simulation_result(const Gallery *g) {
    if (g->lifecycle.failed || g->output.failed)
        return EXIT_FAILURE;
    if (g->lifecycle.stop_signal != 0)
        return GALLERY_SIGNAL_EXIT_BASE + g->lifecycle.stop_signal;
    return EXIT_SUCCESS;
}

int simulation_run(const Config *config) {
    Gallery g = {.config = config,
                 .output = {.console = {.fd = STDOUT_FILENO, .enabled = true, .name = "stdout"},
                            .log = {.fd = -1, .name = "log"}}};
    if (initialize_signals(&g) != 0 || initialize_output(&g) != 0)
        return EXIT_FAILURE;
    if (initialize_sync(&g) != 0)
        return close_output(&g, EXIT_FAILURE);
    int result = EXIT_FAILURE;
    if (prepare_storage(&g) == 0) {
        g.output.started = now(CLOCK_MONOTONIC);
        print_config(&g);
        if (run_workers(&g) == 0) {
            print_summary(&g);
            result = simulation_result(&g);
        }
    }
    release_storage(&g);
    destroy_sync(&g);
    return close_output(&g, result);
}
