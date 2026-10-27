/** @file config.h @brief Конфигурация модели и разбор параметров. */
#ifndef GALLERY_CONFIG_H
#define GALLERY_CONFIG_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

/** @brief Стратегия выбора порядка осмотра картин. */
typedef enum {
    STRATEGY_RANDOM, /**< Случайная перестановка всех картин. */
    STRATEGY_SEQUENTIAL /**< Осмотр в порядке возрастания номеров. */
} PaintingStrategy;

/** @brief Проверенные параметры; все длительности задаются целым числом миллисекунд. */
typedef struct Config {
    unsigned capacity; /**< Максимальное число зарезервированных и занятых мест галереи. */
    unsigned paintings; /**< Число картин в каждом маршруте. */
    unsigned painting_capacity; /**< Число мест возле каждой картины. */
    unsigned visitors; /**< Заданное конечное число посетителей. */
    unsigned arrival_min_ms; /**< Минимальный интервал между прибытиями. */
    unsigned arrival_max_ms; /**< Максимальный интервал между прибытиями. */
    unsigned view_min_ms; /**< Минимальная длительность осмотра. */
    unsigned view_max_ms; /**< Максимальная длительность осмотра. */
    unsigned display_delay_ms; /**< Дополнительная пауза после каждого осмотра для наглядности. */
    uint64_t seed; /**< Общее начальное значение генераторов, независимое от планирования потоков.
                    */
    PaintingStrategy strategy; /**< Стратегия порядка осмотра картин. */
    char *log_path; /**< Необязательный путь журнала, принадлежащий конфигурации. */
    bool has_config_identity; /**< Сохранена идентичность прочитанного файла конфигурации. */
    dev_t config_device; /**< Устройство файла конфигурации; защита от перезаписи журналом. */
    ino_t config_inode; /**< Номер узла файла конфигурации. */
} Config;

/** @brief Разобрать значения по умолчанию, файл key=value и переопределения командной строки.
 * @param argc Число аргументов командной строки.
 * @param argv Массив аргументов.
 * @param config Приёмник параметров; config_destroy освобождает его память и при ошибке.
 * @return 0 при успехе, 1 после --help, -1 при некорректном вводе.
 */
int config_parse(int argc, char **argv, Config *config);

/** @brief Освободить память конфигурации. @param config Разобранная конфигурация. */
void config_destroy(Config *config);

#endif /* GALLERY_CONFIG_H */
