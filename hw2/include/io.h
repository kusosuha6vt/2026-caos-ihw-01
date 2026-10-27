/** @file io.h @brief Вывод через дескрипторы и безопасное открытие журнала. */
#ifndef GALLERY_IO_H
#define GALLERY_IO_H

#include <stddef.h>

/** @brief Предварительное объявление конфигурации без её полей. */
typedef struct Config Config;

/** @brief Проверка формата диагностических сообщений компиляторами GCC и Clang. */
#if defined(__GNUC__) || defined(__clang__)
#define GALLERY_PRINTF_FORMAT(format_index, arguments_index)                                       \
    __attribute__((format(printf, format_index, arguments_index)))
#else
#define GALLERY_PRINTF_FORMAT(format_index, arguments_index)
#endif

/** @brief Записать буфер с повтором при EINTR и частичной записи; готовность канала ждать до 250
 * мс.
 * @param fd Дескриптор назначения.
 * @param data Буфер для записи.
 * @param size Размер буфера в байтах.
 * @return 0 при успехе, -1 при ошибке с установленным errno.
 * @details O_NONBLOCK временно включается и восстанавливается перед возвратом. Флаги относятся
 * к общему описанию открытого файла: параллельно менять их извне нельзя. Срок ожидания готовности
 * канала не ограничивает задержку диска или файловой системы при записи обычного файла.
 */
int fd_write_all(int fd, const char *data, size_t size);

/** @brief Попытаться записать диагностику через дескриптор, завершая сообщение переводом строки.
 * @param format Формат сообщения по правилам printf.
 */
void diagnostic(const char *format, ...) GALLERY_PRINTF_FORMAT(1, 2);

/** @brief Открыть обычный файл журнала, проверив его до усечения.
 * @param config Конфигурация с непустым путём журнала.
 * @return Дескриптор, принадлежащий вызывающей стороне, либо -1 с установленным errno.
 * @details Запрещены символьные и жёсткие ссылки, специальные файлы и совпадение с
 * конфигурацией или стандартными потоками вывода. Новый файл доступен только владельцу.
 */
int open_log_file(const Config *config);

#endif /* GALLERY_IO_H */
