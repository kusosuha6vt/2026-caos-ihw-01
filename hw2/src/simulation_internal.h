/** @file simulation_internal.h @brief Закрытое состояние и взаимодействие модулей модели. */
#ifndef GALLERY_SIMULATION_INTERNAL_H
#define GALLERY_SIMULATION_INTERNAL_H
#include "config.h"
#include "io.h"
#include "permit.h"
#include "random.h"
#include <signal.h>
#include <time.h>

/** @brief Размер записи и единицы перевода времени. */
enum {
    EVENT_BUFFER_CAPACITY = 2048, /**< Размер буфера одного события. */
    MILLISECONDS_PER_SECOND = 1000, /**< Число миллисекунд в секунде. */
    NANOSECONDS_PER_MILLISECOND = 1000000, /**< Число наносекунд в миллисекунде. */
    NANOSECONDS_PER_SECOND = MILLISECONDS_PER_SECOND * NANOSECONDS_PER_MILLISECOND /**< Наносекунды
                                                                                      в секунде. */
};

/** @brief Изменяемая модель; общий доступ разрешён только под её мьютексом. */
typedef struct Gallery Gallery;
/** @brief Состояние посетителя читается и изменяется под мьютексом модели. */
typedef enum {
    CREATED, /**< Поток ещё не зарегистрировал прибытие. */
    QUEUED, /**< Ожидание в очереди у входа. */
    GRANTED, /**< Место галереи зарезервировано вахтёром. */
    INSIDE, /**< Посетитель вошёл и проходит маршрут. */
    FINISHED, /**< Маршрут завершён, место освобождено. */
    CANCELLED /**< Ожидание отменено до допуска. */
} VisitorState;

/** @brief У картины собственные разрешения; счётчики отражают наблюдаемые переходы состояния. */
typedef struct {
    Permit places; /**< Ресурс свободных мест. */
    unsigned viewers; /**< Число начатых и ещё не завершённых осмотров. */
    unsigned maximum; /**< Максимальное наблюдаемое число зрителей. */
} Painting;

/** @brief Поток посетителя и его данные, существующие до присоединения потока. */
typedef struct Visitor {
    Gallery *gallery; /**< Модель-владелец; владение указателем не передаётся. */
    unsigned id; /**< Уникальный идентификатор, начиная с 1. */
    pthread_t thread; /**< Действителен только после успешного создания потока. */
    Permit admission; /**< Личное разрешение от вахтёра посетителю. */
    VisitorState state; /**< Защищённое состояние жизненного цикла. */
    unsigned *route; /**< Собственная перестановка номеров картин. */
    struct Visitor *next; /**< Ссылка на следующий элемент очереди, защищённая мьютексом модели. */
    uint64_t random; /**< Собственное состояние генератора псевдослучайных чисел. */
    unsigned arrival_delay; /**< Назначенный интервал прибытия для воспроизводимости. */
} Visitor;

/** @brief Примитивы синхронизации модели. */
typedef struct {
    pthread_mutex_t mutex; /**< Защищает общее состояние и порядок событий. */
    pthread_cond_t arrival; /**< Прерываемая задержка генератора прибытий. */
    clockid_t wait_clock; /**< Часы для отсчёта задержки прибытия. */
    Permit notifications; /**< Уведомления для повторной проверки состояния вахтёром. */
} GallerySync;

/** @brief Память участников и число успешно подготовленных разрешений. */
typedef struct {
    Painting *pictures; /**< Принадлежащий модели массив картин. */
    Visitor *visitors; /**< Записи посетителей с неизменными адресами. */
    unsigned *routes; /**< Непрерывная область памяти маршрутов. */
    unsigned initialized_pictures; /**< Число инициализированных разрешений картин. */
    unsigned initialized_visitors; /**< Число инициализированных разрешений входа. */
} GalleryStorage;

/** @brief Очередь и текущие ресурсы входа; защищены мьютексом модели. */
typedef struct {
    Visitor *head; /**< Начало очереди. */
    Visitor *tail; /**< Конец очереди. */
    unsigned queued; /**< Число посетителей в очереди. */
    unsigned pending; /**< Число ожидаемых регистраций созданных потоков. */
    unsigned occupied; /**< Зарезервированные и занятые места галереи. */
} EntranceState;

/** @brief Накопленные результаты работы модели. */
typedef struct {
    unsigned maximum; /**< Максимальная занятость галереи с учётом разрешений. */
    unsigned created; /**< Число успешно созданных потоков посетителей. */
    unsigned arrived; /**< Число зарегистрированных прибытий. */
    unsigned admitted; /**< Число разрешений, выданных вахтёром. */
    unsigned completed; /**< Число завершённых маршрутов. */
    unsigned cancelled; /**< Число отменённых ожиданий входа. */
    uint64_t views; /**< Число завершённых осмотров картин. */
} GalleryStatistics;

/** @brief Условия закрытия, остановки и завершения сигнального потока. */
typedef struct {
    bool closed; /**< Генератор больше не создаёт посетителей. */
    bool stopping; /**< Требуется отменить ожидание входа. */
    bool workers_done; /**< Рабочие потоки присоединены, сигнальный поток может выйти. */
    bool failed; /**< Ошибка запуска или выполнения модели. */
    int stop_signal; /**< Первый полученный SIGINT/SIGTERM либо ноль. */
    sigset_t signals; /**< Сигналы, принимаемые через sigwait. */
} GalleryLifecycle;

/** @brief Одно назначение вывода с независимым состоянием ошибки. */
typedef struct {
    int fd; /**< Дескриптор назначения. */
    bool enabled; /**< Запись разрешена и ещё не дала ошибки. */
    const char *name; /**< Фиксированное название для диагностики. */
} OutputSink;

/** @brief Порядок событий, время и назначения вывода. */
typedef struct {
    OutputSink console; /**< Стандартный вывод; дескриптор не принадлежит модели. */
    OutputSink log; /**< Необязательный файл журнала, принадлежащий модели. */
    bool failed; /**< Ошибка хотя бы одного назначения. */
    uint64_t sequence; /**< Номер последнего события. */
    struct timespec started; /**< Начальное показание монотонных часов. */
} GalleryOutput;

/** @brief Дескрипторы служебных потоков, которыми владеет управляющий поток. */
typedef struct {
    pthread_t warden; /**< Поток вахтёра. */
    pthread_t signals; /**< Поток обработки сигналов. */
} WorkerThreads;

/** @brief Контекст модели с отдельными группами состояния. */
struct Gallery {
    const Config *config; /**< Неизменяемая конфигурация без передачи владения. */
    GallerySync sync; /**< Синхронизация общего состояния. */
    GalleryStorage storage; /**< Память и подготовленные разрешения. */
    EntranceState entrance; /**< Очередь, регистрации и занятые места. */
    GalleryStatistics stats; /**< Накопленные результаты. */
    GalleryLifecycle lifecycle; /**< Закрытие и остановка. */
    GalleryOutput output; /**< Вывод событий и его состояние. */
};

/** @brief Захватить мьютекс модели. @param g Модель. */
static inline void lock(Gallery *g) {
    sync_check(pthread_mutex_lock(&g->sync.mutex), "model lock");
}

/** @brief Освободить мьютекс модели. @param g Модель. */
static inline void unlock(Gallery *g) {
    sync_check(pthread_mutex_unlock(&g->sync.mutex), "model unlock");
}

/** @brief Выбрать целочисленную задержку с включением границ диапазона. @param state Собственное
 * состояние генератора псевдослучайных чисел.
 * @param minimum Нижняя граница. @param maximum Верхняя граница. @return Миллисекунды.
 */
static inline unsigned sample(uint64_t *state, unsigned minimum, unsigned maximum) {
    return minimum + (unsigned)random_below(state, (uint64_t)maximum - minimum + 1);
}

/** @brief Получить показание часов с проверкой ошибки. @param clock Идентификатор часов. @return
 * Показание часов. */
struct timespec now(clockid_t clock);

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
                         unsigned delay);

/** @brief Поток вахтёра: обслуживание входа и проверка завершения рабочего дня.
 * @param argument Указатель на модель. @return NULL.
 */
void *warden_main(void *argument);

/** @brief Поток посетителя: вход, осмотр маршрута и выход.
 * @param argument Указатель на Visitor. @return NULL.
 */
void *visitor_main(void *argument);

/** @brief Вывести конфигурацию и название реализации синхронизации до событий. @param g Модель. */
void print_config(Gallery *g);

/** @brief Вывести максимумы картин и общие итоги. @param g Модель после присоединения рабочих
 * потоков. */
void print_summary(Gallery *g);

/** @brief Открыть необязательный журнал до запуска модели.
 * @param g Модель с начальными назначениями вывода. @return 0 либо -1.
 */
int initialize_output(Gallery *g);

/** @brief Закрыть принадлежащий модели журнал с сохранением ошибки в коде выхода.
 * @param g Модель без работающих потоков. @param result Предварительный код выхода.
 * @return Итоговый код выхода.
 */
int close_output(Gallery *g, int result);

#endif /* GALLERY_SIMULATION_INTERNAL_H */
