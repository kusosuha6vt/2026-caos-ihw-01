/** @file io.c @brief Вывод через дескрипторы с ограниченным размером диагностических сообщений. */
#include "array_utils.h"
#include "io.h"
#include "config.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/** @brief Размер буфера для одного диагностического сообщения. */
enum {
    DIAGNOSTIC_BUFFER_CAPACITY = 1024, /**< Размер диагностического сообщения. */
    WRITE_TIMEOUT_MS = 250, /**< Предельное ожидание готовности назначения. */
    MILLISECONDS_PER_SECOND = 1000, /**< Миллисекунд в секунде. */
    NANOSECONDS_PER_MILLISECOND = 1000000 /**< Наносекунд в миллисекунде. */
};

/** @brief Монотонное время в миллисекундах. @return Время либо -1 с errno. */
static int64_t monotonic_ms(void) {
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0)
        return -1;
    return (int64_t)value.tv_sec * MILLISECONDS_PER_SECOND +
           value.tv_nsec / NANOSECONDS_PER_MILLISECOND;
}

/** @brief Записать буфер неблокирующими операциями до общего срока.
 * @param fd Дескриптор. @param data Буфер. @param size Размер.
 * @return 0 либо -1 с errno; обычный файловый ввод-вывод не ограничивается poll.
 */
static int write_until_ready(int fd, const char *data, size_t size) {
    int64_t start = monotonic_ms();
    if (start < 0)
        return -1;
    while (size != 0) {
        ssize_t written = write(fd, data, size);
        if (written > 0) {
            data += (size_t)written;
            size -= (size_t)written;
            continue;
        }
        if (written == 0) {
            errno = EIO;
            return -1;
        }
        if (errno == EINTR)
            continue;
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            return -1;
        int64_t current = monotonic_ms();
        if (current < 0)
            return -1;
        int64_t remaining = WRITE_TIMEOUT_MS - (current - start);
        if (remaining <= 0) {
            errno = ETIMEDOUT;
            return -1;
        }
        struct pollfd destination = {.fd = fd, .events = POLLOUT};
        if (poll(&destination, 1, (int)remaining) < 0 && errno != EINTR)
            return -1;
    }
    return 0;
}

int fd_write_all(int fd, const char *data, size_t size) {
    int flags = fcntl(fd, F_GETFL);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        return -1;
    int result = write_until_ready(fd, data, size);
    int error = errno;
    if (fcntl(fd, F_SETFL, flags) < 0 && result == 0)
        return -1;
    errno = error;
    return result;
}

void diagnostic(const char *format, ...) {
    char buffer[DIAGNOSTIC_BUFFER_CAPACITY];
    va_list arguments;
    va_start(arguments, format);
    int count = vsnprintf(buffer, sizeof(buffer) - 1, format, arguments);
    va_end(arguments);
    if (count < 0)
        return;
    size_t length = (size_t)count;
    if (length >= sizeof(buffer) - 1)
        length = sizeof(buffer) - 2;
    buffer[length++] = '\n';
    (void)fd_write_all(STDERR_FILENO, buffer, length);
}

int open_log_file(const Config *config) {
    if (config->log_path == NULL || *config->log_path == '\0') {
        errno = EINVAL;
        return -1;
    }
    int fd = open(config->log_path,
                  O_WRONLY | O_CREAT | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK,
                  S_IRUSR | S_IWUSR);
    if (fd < 0)
        return -1;
    struct stat file;
    if (fstat(fd, &file) != 0)
        goto fail;
    if (!S_ISREG(file.st_mode) || file.st_nlink != 1 ||
        (config->has_config_identity && file.st_dev == config->config_device &&
         file.st_ino == config->config_inode)) {
        errno = EINVAL;
        goto fail;
    }
    const int output_descriptors[] = {STDOUT_FILENO, STDERR_FILENO};
    for (size_t i = 0; i < GALLERY_ARRAY_COUNT(output_descriptors); ++i) {
        struct stat output;
        if (fstat(output_descriptors[i], &output) != 0) {
            if (errno == EBADF)
                continue;
            goto fail;
        }
        if (file.st_dev == output.st_dev && file.st_ino == output.st_ino) {
            errno = EINVAL;
            goto fail;
        }
    }
    if (ftruncate(fd, 0) == 0)
        return fd;
fail : {
    int error = errno;
    (void)close(fd);
    errno = error;
    return -1;
}
}
