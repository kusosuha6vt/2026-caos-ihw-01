/** @file permit.c @brief Две реализации одного интерфейса счётных разрешений. */
#include "permit.h"
#include "io.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

void sync_check(int error, const char *operation) {
    if (error != 0) {
        diagnostic("Fatal synchronization error: %s: %s", operation, strerror(error));
        _Exit(EXIT_FAILURE);
    }
}

int permit_init(Permit *permit, unsigned initial) {
#ifdef GALLERY_TESTING
    /* Разрешения создаются управляющим потоком до запуска участников. */
    static unsigned initialization_count;
    const char *fail_after = getenv("GALLERY_FAIL_PERMIT_AFTER");
    if (fail_after != NULL && strtoul(fail_after, NULL, 10) == initialization_count)
        return ENOMEM;
    ++initialization_count;
#endif
#if GALLERY_COND
    int error = pthread_mutex_init(&permit->mutex, NULL);
    if (error != 0)
        return error;
    error = pthread_cond_init(&permit->changed, NULL);
    if (error != 0) {
        (void)pthread_mutex_destroy(&permit->mutex);
        return error;
    }
    permit->available = initial;
    return 0;
#else
    return sem_init(&permit->semaphore, 0, initial) == 0 ? 0 : errno;
#endif
}

void permit_destroy(Permit *permit) {
#if GALLERY_COND
    sync_check(pthread_cond_destroy(&permit->changed), "condition destroy");
    sync_check(pthread_mutex_destroy(&permit->mutex), "permit mutex destroy");
#else
    if (sem_destroy(&permit->semaphore) != 0)
        sync_check(errno, "sem_destroy");
#endif
}

void permit_wait(Permit *permit) {
#if GALLERY_COND
    sync_check(pthread_mutex_lock(&permit->mutex), "permit lock");
    while (permit->available == 0)
        sync_check(pthread_cond_wait(&permit->changed, &permit->mutex), "permit wait");
    --permit->available;
    sync_check(pthread_mutex_unlock(&permit->mutex), "permit unlock");
#else
    while (sem_wait(&permit->semaphore) != 0) {
        if (errno != EINTR)
            sync_check(errno, "sem_wait");
    }
#endif
}

bool permit_try(Permit *permit) {
#if GALLERY_COND
    sync_check(pthread_mutex_lock(&permit->mutex), "permit try lock");
    bool available = permit->available != 0;
    if (available)
        --permit->available;
    sync_check(pthread_mutex_unlock(&permit->mutex), "permit try unlock");
    return available;
#else
    for (;;) {
        if (sem_trywait(&permit->semaphore) == 0)
            return true;
        if (errno == EAGAIN)
            return false;
        if (errno != EINTR)
            sync_check(errno, "sem_trywait");
    }
#endif
}

void permit_post(Permit *permit) {
#if GALLERY_COND
    sync_check(pthread_mutex_lock(&permit->mutex), "permit post lock");
    if (permit->available == UINT_MAX)
        sync_check(EOVERFLOW, "permit overflow");
    ++permit->available;
    sync_check(pthread_cond_signal(&permit->changed), "permit signal");
    sync_check(pthread_mutex_unlock(&permit->mutex), "permit post unlock");
#else
    if (sem_post(&permit->semaphore) != 0)
        sync_check(errno, "sem_post");
#endif
}
