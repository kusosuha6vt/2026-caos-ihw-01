/** @file permit.h @brief Счётные разрешения на семафорах POSIX или условных переменных. */
#ifndef PERMIT_H
#define PERMIT_H
#include <stdbool.h>
#include <pthread.h>
#ifndef GALLERY_COND
/** @brief При сборке задать 1 для замены семафоров мониторами с условными переменными. */
#define GALLERY_COND 0
#endif
#if GALLERY_COND
/** @brief Счётный монитор без семафоров; все поля защищены мьютексом. */
typedef struct {
    pthread_mutex_t mutex; /**< Защищает счётчик available. */
    pthread_cond_t changed; /**< Уведомляет о доступности разрешения. */
    unsigned available; /**< Число доступных разрешений. */
} Permit;
#else
#include <semaphore.h>
/** @brief Неименованный семафор POSIX, принадлежащий объекту модели. */
typedef struct {
    sem_t semaphore; /**< Состояние семафора в ядре или libc. */
} Permit;
#endif

/** @brief Инициализировать счётчик разрешений. @param permit Приёмник результата.
 * @param initial Начальное значение счётчика. @return 0 или код ошибки POSIX.
 */
int permit_init(Permit *permit, unsigned initial);
/** @brief Уничтожить инициализированный счётчик, который больше не используется. @param permit
 * Счётчик. */
void permit_destroy(Permit *permit);
/** @brief Дождаться и получить одно разрешение; повторить ожидание при EINTR. @param permit
 * Счётчик. */
void permit_wait(Permit *permit);
/** @brief Получить разрешение без ожидания. @param permit Счётчик.
 * @return true при успехе, false при отсутствии доступных разрешений.
 */
bool permit_try(Permit *permit);
/** @brief Вернуть одно разрешение и пробудить ожидающего. @param permit Счётчик. */
void permit_post(Permit *permit);
/** @brief Аварийно завершить программу при непредвиденной ошибке pthread или синхронизации.
 * @param error Код ошибки POSIX; нулевой код игнорируется.
 * @param operation Понятное пользователю название операции.
 * @details После отказа примитива синхронизации его дальнейшее безопасное использование невозможно.
 */
void sync_check(int error, const char *operation);
#endif
