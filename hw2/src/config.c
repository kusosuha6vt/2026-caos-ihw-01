/** @file config.c @brief Строгий разбор командной строки и файла key=value через дескрипторы. */
#include "array_utils.h"
#include "config.h"
#include "exit_status.h"
#include "io.h"
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include "ini.h"
#include <inttypes.h>
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/** @brief Технические размеры и ограничения разбора конфигурации. */
enum {
    CONFIG_LINE_CAPACITY = 4096, /**< Размер строки с завершающим нулём. */
    CONFIG_MAX_FILE_BYTES = 65536, /**< Максимальный объём файла конфигурации. */
    CONFIG_MAX_PAINTINGS = 1024, /**< Максимальное число картин. */
    CONFIG_MAX_VISITORS = 4096, /**< Максимальное число посетителей. */
    CONFIG_MAX_DELAY_MS = 3600000, /**< Максимальная задержка в миллисекундах. */
    HELP_BUFFER_CAPACITY = 2048, /**< Размер буфера справки. */
    DECIMAL_BASE = 10 /**< Основание десятичной системы. */
};

/** @brief Этапы разбора: файл читается до переопределений командной строки. */
typedef enum {
    CONFIG_PASS_FILE, /**< Найти и прочитать файл конфигурации. */
    CONFIG_PASS_ARGUMENTS, /**< Применить аргументы командной строки. */
    CONFIG_PASS_COUNT /**< Число этапов разбора. */
} ConfigPass;

/** @brief Настройки запуска по умолчанию. */
static const Config DEFAULT_CONFIG = {.capacity = 10,
                                      .paintings = 5,
                                      .painting_capacity = 3,
                                      .visitors = 100,
                                      .arrival_min_ms = 1,
                                      .arrival_max_ms = 3,
                                      .view_min_ms = 1,
                                      .view_max_ms = 5,
                                      .seed = 42,
                                      .strategy = STRATEGY_RANDOM};

/** @brief Соответствие имён параметров беззнаковым полям конфигурации. */
typedef struct {
    const char *name; /**< Имя параметра без начальных дефисов. */
    size_t offset; /**< Смещение беззнакового поля в Config. */
    unsigned minimum; /**< Включённая нижняя граница. */
    unsigned maximum; /**< Включённая техническая верхняя граница. */
} NumberOption;

/** @brief Явные ограничения ресурсов данной реализации, не требования задания. */
static const NumberOption NUMBER_OPTIONS[] = {
    {"capacity", offsetof(Config, capacity), 1, INT_MAX},
    {"paintings", offsetof(Config, paintings), 1, CONFIG_MAX_PAINTINGS},
    {"painting-capacity", offsetof(Config, painting_capacity), 1, INT_MAX},
    {"visitors", offsetof(Config, visitors), 0, CONFIG_MAX_VISITORS},
    {"arrival-min-ms", offsetof(Config, arrival_min_ms), 0, CONFIG_MAX_DELAY_MS},
    {"arrival-max-ms", offsetof(Config, arrival_max_ms), 0, CONFIG_MAX_DELAY_MS},
    {"view-min-ms", offsetof(Config, view_min_ms), 0, CONFIG_MAX_DELAY_MS},
    {"view-max-ms", offsetof(Config, view_max_ms), 0, CONFIG_MAX_DELAY_MS},
    {"display-delay-ms", offsetof(Config, display_delay_ms), 0, CONFIG_MAX_DELAY_MS}};

/** @brief Разобрать десятичное uint64, запретив знаки, пробелы и посторонний суффикс.
 * @param text Входная строка. @param value Приёмник результата. @return Успешен ли разбор.
 */
static bool parse_number(const char *text, uint64_t *value) {
    if (*text == '\0')
        return false;
    for (const char *p = text; *p != '\0'; ++p)
        if (*p < '0' || *p > '9')
            return false;
    errno = 0;
    char *end;
    uintmax_t number = strtoumax(text, &end, DECIMAL_BASE);
    if (errno == ERANGE || *end != '\0' || number > UINT64_MAX)
        return false;
    *value = (uint64_t)number;
    return true;
}

/** @brief Проверить и присвоить беззнаковый числовой параметр.
 * @param config Приёмник параметров. @param option Описание поля и его границ.
 * @param value Текст числа. @return 0 при успехе, -1 при ошибке.
 */
static int set_number_option(Config *config, const NumberOption *option, const char *value) {
    uint64_t number;
    if (!parse_number(value, &number) || number < option->minimum || number > option->maximum) {
        diagnostic("Invalid %s: expected decimal %u..%u",
                   option->name,
                   option->minimum,
                   option->maximum);
        return -1;
    }
    unsigned *field = (unsigned *)((char *)config + option->offset);
    *field = (unsigned)number;
    return 0;
}

/** @brief Установить стратегию по её внешнему обозначению.
 * @param config Приёмник параметров. @param value Обозначение стратегии. @return 0 либо -1.
 */
static int set_strategy(Config *config, const char *value) {
    if (strcmp(value, "random") == 0)
        config->strategy = STRATEGY_RANDOM;
    else if (strcmp(value, "sequential") == 0)
        config->strategy = STRATEGY_SEQUENTIAL;
    else {
        diagnostic("Invalid strategy: expected random or sequential");
        return -1;
    }
    return 0;
}

/** @brief Скопировать новый путь журнала до освобождения предыдущего.
 * @param config Приёмник параметров. @param value Непустой путь. @return 0 либо -1.
 */
static int set_log_path(Config *config, const char *value) {
    if (*value == '\0') {
        diagnostic("Log path must not be empty");
        return -1;
    }
    char *copy = strdup(value);
    if (copy == NULL) {
        diagnostic("Cannot allocate log path");
        return -1;
    }
    free(config->log_path);
    config->log_path = copy;
    return 0;
}

/** @brief Найти описание параметра и передать значение подходящему обработчику.
 * @param config Приёмник параметров. @param key Имя параметра. @param value Текст значения.
 * @return 0 при успехе, -1 при ошибке.
 */
static int set_option(Config *config, const char *key, const char *value) {
    for (size_t i = 0; i < GALLERY_ARRAY_COUNT(NUMBER_OPTIONS); ++i) {
        if (strcmp(key, NUMBER_OPTIONS[i].name) == 0)
            return set_number_option(config, &NUMBER_OPTIONS[i], value);
    }
    if (strcmp(key, "strategy") == 0)
        return set_strategy(config, value);
    if (strcmp(key, "log") == 0)
        return set_log_path(config, value);
    if (strcmp(key, "seed") == 0) {
        if (parse_number(value, &config->seed))
            return 0;
        diagnostic("Invalid seed: expected decimal uint64");
    } else {
        diagnostic("Unknown option: %s", key);
    }
    return -1;
}

/** @brief Применить одну пару INI; секции в плоской конфигурации запрещены.
 * @param user Конфигурация. @param section Секция. @param name Имя параметра.
 * @param value Значение. @return 1 при успехе, 0 при ошибке.
 */
static int config_entry(void *user, const char *section, const char *name, const char *value) {
    if (*section != '\0' || name == NULL || value == NULL) {
        diagnostic("Config sections are not supported");
        return 0;
    }
    return set_option(user, name, value) == 0;
}

/** @brief Открыть и проверить обычный файл конфигурации без блокировки на специальных файлах.
 * @param path Путь к файлу. @param file Полученные метаданные.
 * @return Дескриптор, принадлежащий вызывающей стороне, либо -1.
 */
static int open_config_input(const char *path, struct stat *file) {
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) {
        diagnostic("Cannot open config %s: %s", path, strerror(errno));
        return -1;
    }
    if (fstat(fd, file) != 0) {
        diagnostic("Cannot inspect config: %s", strerror(errno));
        (void)close(fd);
        return -1;
    }
    if (!S_ISREG(file->st_mode) || file->st_size < 0 ||
        (uintmax_t)file->st_size > CONFIG_MAX_FILE_BYTES) {
        diagnostic("Config must be a regular file of at most %u bytes", CONFIG_MAX_FILE_BYTES);
        (void)close(fd);
        return -1;
    }
    return fd;
}

/** @brief Прочитать ограниченный текст через дескриптор и передать его inih.
 * @param config Приёмник параметров. @param fd Проверенный обычный файл.
 * @return 0 при успехе, -1 при ошибке чтения или данных.
 */
static int parse_config_input(Config *config, int fd) {
    char *text = malloc(CONFIG_MAX_FILE_BYTES + 1);
    if (text == NULL) {
        diagnostic("Cannot allocate config buffer");
        return -1;
    }
    size_t used = 0, line_length = 0;
    int result = -1;
    for (;;) {
        ssize_t count = read(fd, text + used, CONFIG_MAX_FILE_BYTES + 1 - used);
        if (count < 0) {
            if (errno == EINTR)
                continue;
            diagnostic("Cannot read config: %s", strerror(errno));
            break;
        }
        if (count == 0) {
            text[used] = '\0';
            int error = ini_parse_string(text, config_entry, config);
            if (error == 0)
                result = 0;
            else
                diagnostic("Invalid config at line %d", error);
            break;
        }
        used += (size_t)count;
        if (used > CONFIG_MAX_FILE_BYTES) {
            diagnostic("Config exceeds %u bytes", CONFIG_MAX_FILE_BYTES);
            break;
        }
        bool valid = true;
        for (size_t i = used - (size_t)count; i < used; ++i) {
            line_length = text[i] == '\n' ? 0 : line_length + 1;
            if (text[i] == '\0' || line_length >= CONFIG_LINE_CAPACITY) {
                diagnostic("Config contains NUL or a line exceeds %u bytes",
                           CONFIG_LINE_CAPACITY - 1);
                valid = false;
                break;
            }
        }
        if (!valid)
            break;
    }
    free(text);
    return result;
}

/** @brief Прочитать конфигурацию, сохранив идентичность открытого файла.
 * @param config Приёмник параметров. @param path Путь. @return 0 либо -1.
 */
static int read_config(Config *config, const char *path) {
    struct stat file;
    int fd = open_config_input(path, &file);
    if (fd < 0)
        return -1;
    int result = parse_config_input(config, fd);
    if (result == 0) {
        config->has_config_identity = true;
        config->config_device = file.st_dev;
        config->config_inode = file.st_ino;
    }
    if (close(fd) != 0) {
        diagnostic("Cannot close config: %s", strerror(errno));
        result = -1;
    }
    return result;
}

/** @brief Вывести справку, подставив действующие значения по умолчанию и пределы.
 * @return 1 после успешной записи, -1 при ошибке.
 */
static int print_help(void) {
    char help[HELP_BUFFER_CAPACITY];
    int count = snprintf(
        help,
        sizeof(help),
        "Gallery: finite Linux pthread simulation. Output: JSON Lines with Russian event text.\n"
        "Usage: gallery [--config FILE] [--OPTION VALUE | --OPTION=VALUE]\n"
        "Defaults: capacity=%u paintings=%u painting-capacity=%u visitors=%u\n"
        "  arrival-min-ms=%u arrival-max-ms=%u view-min-ms=%u view-max-ms=%u\n"
        "  strategy=random seed=%" PRIu64 " display-delay-ms=%u\n"
        "Options: --capacity --paintings --painting-capacity --visitors\n"
        "  --arrival-min-ms --arrival-max-ms --view-min-ms --view-max-ms\n"
        "  --strategy random|sequential --seed --display-delay-ms --log FILE\n"
        "Limits: visitors 0..%u, paintings 1..%u, capacities 1..%u,\n"
        "  durations 0..%u ms; seed is uint64. Min must not exceed max.\n"
        "Config: regular file up to %u bytes, key=value, # comments.\n"
        "  defaults < file < CLI. One --config allowed.\n"
        "Log: regular file only; no symlinks, hard links or input/output aliases.\n"
        "SIGINT/SIGTERM: cancel entrance queue, finish admitted routes; exit %u+signal.\n",
        DEFAULT_CONFIG.capacity,
        DEFAULT_CONFIG.paintings,
        DEFAULT_CONFIG.painting_capacity,
        DEFAULT_CONFIG.visitors,
        DEFAULT_CONFIG.arrival_min_ms,
        DEFAULT_CONFIG.arrival_max_ms,
        DEFAULT_CONFIG.view_min_ms,
        DEFAULT_CONFIG.view_max_ms,
        DEFAULT_CONFIG.seed,
        DEFAULT_CONFIG.display_delay_ms,
        CONFIG_MAX_VISITORS,
        CONFIG_MAX_PAINTINGS,
        (unsigned)INT_MAX,
        CONFIG_MAX_DELAY_MS,
        CONFIG_MAX_FILE_BYTES,
        GALLERY_SIGNAL_EXIT_BASE);
    if (count < 0 || (size_t)count >= sizeof(help)) {
        diagnostic("Cannot format help");
        return -1;
    }
    return fd_write_all(STDOUT_FILENO, help, (size_t)count) == 0 ? 1 : -1;
}

/** @brief Длинные параметры getopt_long; короткая форма есть только у справки. */
static const struct option CLI_OPTIONS[] = {{"capacity", required_argument, NULL, 1},
                                            {"paintings", required_argument, NULL, 1},
                                            {"painting-capacity", required_argument, NULL, 1},
                                            {"visitors", required_argument, NULL, 1},
                                            {"arrival-min-ms", required_argument, NULL, 1},
                                            {"arrival-max-ms", required_argument, NULL, 1},
                                            {"view-min-ms", required_argument, NULL, 1},
                                            {"view-max-ms", required_argument, NULL, 1},
                                            {"display-delay-ms", required_argument, NULL, 1},
                                            {"strategy", required_argument, NULL, 1},
                                            {"seed", required_argument, NULL, 1},
                                            {"log", required_argument, NULL, 1},
                                            {"config", required_argument, NULL, 1},
                                            {"help", no_argument, NULL, 'h'},
                                            {NULL, 0, NULL, 0}};

/** @brief Выполнить проход getopt_long без перестановки аргументов.
 * @param argc Число аргументов. @param argv Аргументы. @param pass Назначение прохода.
 * @param config Приёмник. @param path Путь единственного файла конфигурации.
 * @return 0 при успехе, 1 после справки, -1 при ошибке.
 */
static int
parse_arguments(int argc, char **argv, ConfigPass pass, Config *config, const char **path) {
#ifdef __APPLE__
    optreset = 1;
    optind = 1;
#else
    optind = 0;
#endif
    opterr = 0;
    int option, index = 0;
    while ((option = getopt_long(argc, argv, "+h", CLI_OPTIONS, &index)) != -1) {
        if (option == 'h')
            return print_help();
        if (option != 1) {
            diagnostic("Invalid option or missing value: %s", argv[optind - 1]);
            return -1;
        }
        const char *name = CLI_OPTIONS[index].name;
        if (strcmp(name, "config") == 0) {
            if (pass == CONFIG_PASS_FILE) {
                if (*path != NULL) {
                    diagnostic("Only one --config is allowed");
                    return -1;
                }
                *path = optarg;
            }
        } else if (pass == CONFIG_PASS_ARGUMENTS && set_option(config, name, optarg) != 0)
            return -1;
    }
    if (optind != argc) {
        diagnostic("Unexpected positional argument: %s", argv[optind]);
        return -1;
    }
    return 0;
}

int config_parse(int argc, char **argv, Config *config) {
    *config = DEFAULT_CONFIG;
    const char *path = NULL;
    for (ConfigPass pass = CONFIG_PASS_FILE; pass < CONFIG_PASS_COUNT; ++pass) {
        int result = parse_arguments(argc, argv, pass, config, &path);
        if (result != 0)
            return result;
        if (pass == CONFIG_PASS_FILE && path != NULL && read_config(config, path) != 0)
            return -1;
    }
    if (config->arrival_min_ms > config->arrival_max_ms ||
        config->view_min_ms > config->view_max_ms) {
        diagnostic("Minimum delay must not exceed maximum delay");
        return -1;
    }
    return 0;
}

void config_destroy(Config *config) {
    free(config->log_path);
    config->log_path = NULL;
}
