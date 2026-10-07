/** @file config.c
 * @brief Configuration defaults, checked parsing and precedence.
 */

#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum {
    CAPACITY,
    PAINTINGS,
    PAINTING_CAPACITY,
    VISITORS,
    ARRIVAL_MIN,
    ARRIVAL_MAX,
    VIEW_MIN,
    VIEW_MAX,
    SEED,
    DELAY,
    MAX_ACTIVE,
    STRATEGY,
    LOG,
    JOURNAL,
    UNLIMITED,
    QUIET,
    JSON,
    FIELD_COUNT
};

static const char *keys[FIELD_COUNT] = {
    "GALLERY_CAPACITY", "GALLERY_PAINTINGS",   "GALLERY_PAINTING_CAPACITY",
    "GALLERY_VISITORS", "GALLERY_ARRIVAL_MIN", "GALLERY_ARRIVAL_MAX",
    "GALLERY_VIEW_MIN", "GALLERY_VIEW_MAX",    "GALLERY_SEED",
    "GALLERY_DELAY_MS", "GALLERY_MAX_ACTIVE",  "GALLERY_STRATEGY",
    "GALLERY_LOG",      "GALLERY_JOURNAL",     "GALLERY_UNLIMITED",
    "GALLERY_QUIET",    "GALLERY_JSON"};

#define ARG(name, field) {name, required_argument, NULL, 1000 + (field)}
static const struct option options[] = {
    ARG("capacity", CAPACITY),
    ARG("paintings", PAINTINGS),
    ARG("painting-capacity", PAINTING_CAPACITY),
    ARG("visitors", VISITORS),
    ARG("arrival-min", ARRIVAL_MIN),
    ARG("arrival-max", ARRIVAL_MAX),
    ARG("view-min", VIEW_MIN),
    ARG("view-max", VIEW_MAX),
    ARG("seed", SEED),
    ARG("delay-ms", DELAY),
    ARG("max-active", MAX_ACTIVE),
    ARG("strategy", STRATEGY),
    ARG("log", LOG),
    ARG("journal", JOURNAL),
    {"unlimited", no_argument, NULL, 1000 + UNLIMITED},
    {"quiet", no_argument, NULL, 1000 + QUIET},
    {"json", no_argument, NULL, 1000 + JSON},
    {"config", required_argument, NULL, 'c'},
    {"interactive", no_argument, NULL, 'i'},
    {"finite", no_argument, NULL, 'f'},
    {"help", no_argument, NULL, 'h'},
    {NULL, 0, NULL, 0}};
#undef ARG

/** @brief Print the Russian CLI help and documented input/exit conventions.
 */
static void help(void) {
    puts(
        "Gallery: последовательная имитация, один процесс / один поток.\n"
        "  --capacity N            вместимость галереи (1..10000), default 20\n"
        "  --paintings N           число картин (1..256), default 7\n"
        "  --painting-capacity N   зрителей у картины (1..10000), default 3\n"
        "  --visitors N            посетителей (0..100000), default 200\n"
        "  --arrival-min N --arrival-max N  интервал прибытия, мс "
        "(0..1000000)\n"
        "  --view-min N --view-max N        осмотр, мс (1..1000000)\n"
        "  --strategy ordered|random|least-crowded|cyclic\n"
        "  --seed N                uint64 seed, default 42\n"
        "  --delay-ms N            задержка вывода (0..60000), не время "
        "модели\n"
        "  --max-active N          лимит записей посетителей (1..10000)\n"
        "  --unlimited | --finite  бесконечные прибытия / конечный режим\n"
        "  --config FILE           данные KEY=VALUE, не shell-скрипт\n"
        "  --interactive           запросить основные параметры через stdin\n"
        "  --log FILE              JSONL событий, default gallery.jsonl; '-' "
        "отключает\n"
        "  --journal FILE          CSV результатов, default results.csv; '-' "
        "отключает\n"
        "  --json                  JSONL в stdout вместо обычных сообщений\n"
        "  --quiet                 только итоговый JSON в stdout\n"
        "  --help                  эта справка\n"
        "Приоритет: defaults < config < GALLERY_* environment < CLI < "
        "interactive.\n"
        "Интервалы default: arrival 1..5, view 10..30 мс.\n"
        "Выход: 0 завершение; 1 ошибка выполнения; 2 входные данные;\n"
        "130 SIGINT; 143 SIGTERM. В очередях FIFO; завершения раньше "
        "прибытий.\n"
        "Пример: gallery --visitors 200 --strategy cyclic --seed 7 --delay-ms "
        "0");
}

/** @brief Parse a checked unsigned decimal integer.
 * @param text Nonempty decimal input; signs and trailing characters are
 * rejected.
 * @param[out] value Parsed value, written only on success.
 * @return 0 on success, -1 on invalid input or overflow.
 */
static int number(const char *text, uint64_t *value) {
    if (!text || !isdigit((unsigned char)text[0]))
        return -1;
    errno = 0;
    char *end;
    uintmax_t parsed = strtoumax(text, &end, 10);
    if (errno || *end || parsed > UINT64_MAX)
        return -1;
    *value = (uint64_t)parsed;
    return 0;
}

/* Token-only arguments for set_field's numeric cases; n is parsed once there.
 */
#define NUMBER_CASE(key, member)                                               \
    case (key):                                                                \
        c->member = n;                                                         \
        return 0;

/* One setter serves CLI, environment and file: their validation cannot diverge.
 */
/** @brief Apply one configuration field using shared validation.
 * @param[in,out] c Configuration receiving the value.
 * @param field Recognized field index in the range [0, FIELD_COUNT).
 * @param text String value supplied by a file, environment, CLI or prompt.
 * @return 0 on success, -1 after reporting an invalid value.
 * @details Numeric bounds spanning multiple fields are checked by
 * config_read().
 */
static int set_field(Config *c, int field, const char *text) {
    uint64_t n = 0;
    if (field == STRATEGY) {
        for (int i = ORDERED; i <= CYCLIC; ++i)
            if (!strcmp(text, strategy_name((Strategy)i))) {
                c->strategy = (Strategy)i;
                return 0;
            }
    } else if (field == LOG || field == JOURNAL) {
        char *target = field == LOG ? c->log_path : c->journal_path;
        if (*text && strlen(text) < sizeof(c->log_path)) {
            strcpy(target, text);
            return 0;
        }
    } else if (!number(text, &n)) {
        switch (field) {
            NUMBER_CASE(CAPACITY, capacity)
            NUMBER_CASE(PAINTINGS, paintings)
            NUMBER_CASE(PAINTING_CAPACITY, painting_capacity)
            NUMBER_CASE(VISITORS, visitors)
            NUMBER_CASE(ARRIVAL_MIN, arrival_min)
            NUMBER_CASE(ARRIVAL_MAX, arrival_max)
            NUMBER_CASE(VIEW_MIN, view_min)
            NUMBER_CASE(VIEW_MAX, view_max)
            NUMBER_CASE(SEED, seed)
            NUMBER_CASE(DELAY, delay_ms)
            NUMBER_CASE(MAX_ACTIVE, max_active)
        case UNLIMITED:
            if (n <= 1) {
                c->unlimited = (int)n;
                return 0;
            }
            break;
        case QUIET:
            if (n <= 1) {
                c->quiet = (int)n;
                return 0;
            }
            break;
        case JSON:
            if (n <= 1) {
                c->json = (int)n;
                return 0;
            }
            break;
        default:
            break;
        }
    }
    fprintf(stderr, "Некорректное значение %s: %s\n", keys[field], text);
    return -1;
}
#undef NUMBER_CASE

/** @brief Trim leading/trailing whitespace in a mutable string.
 * @param[in,out] text NUL-terminated buffer; trailing whitespace is
 * overwritten.
 * @return Borrowed pointer into text at the first non-whitespace character.
 */
static char *trim(char *text) {
    while (isspace((unsigned char)*text))
        ++text;
    char *end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1]))
        --end;
    *end = '\0';
    return text;
}

/** @brief Read a KEY=VALUE file as data, rejecting malformed or duplicate keys.
 * @param[in,out] c Configuration receiving recognized fields.
 * @param path Configuration file path.
 * @return 0 on success, -1 on parsing, read or close failure.
 * @details Rejects embedded NUL and oversized lines; never evaluates shell
 * syntax.
 */
static int read_file(Config *c, const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror(path);
        return -1;
    }
    FILE *input = fdopen(fd, "r");
    if (!input) {
        perror("fdopen");
        close(fd);
        return -1;
    }
    char *line = NULL;
    size_t allocated = 0;
    ssize_t length;
    uint64_t seen = 0;
    unsigned line_number = 0;
    int result = 0;
    while ((length = getline(&line, &allocated, input)) >= 0) {
        ++line_number;
        if (length > GALLERY_MAX_INPUT_LINE || (size_t)length != strlen(line)) {
            result = -1;
            break;
        }
        char *entry = trim(line);
        if (!*entry || *entry == '#')
            continue;
        char *separator = strchr(entry, '=');
        if (!separator) {
            result = -1;
            break;
        }
        *separator = '\0';
        char *key = trim(entry), *value = trim(separator + 1);
        int field;
        for (field = 0; field < FIELD_COUNT; ++field)
            if (!strcmp(key, keys[field]))
                break;
        if (field == FIELD_COUNT || (seen & (UINT64_C(1) << field))) {
            result = -1;
            break;
        }
        seen |= UINT64_C(1) << field;
        if (set_field(c, field, value)) {
            result = -1;
            break;
        }
    }
    if (ferror(input))
        result = -1;
    if (result)
        fprintf(stderr, "Ошибка config %s, строка %u\n", path, line_number);
    free(line);
    if (fclose(input))
        result = -1;
    return result;
}

/** @brief Prompt for primary parameters and validate each response.
 * @param[in,out] c Configuration receiving prompted fields.
 * @return 0 on success, -1 on EOF, invalid input or input failure.
 */
static int interactive(Config *c) {
    static const int fields[] = {CAPACITY, PAINTINGS,   PAINTING_CAPACITY,
                                 VISITORS, ARRIVAL_MIN, ARRIVAL_MAX,
                                 VIEW_MIN, VIEW_MAX,    STRATEGY};
    char *line = NULL;
    size_t size = 0;
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
        fprintf(stderr, "%s: ", keys[fields[i]]);
        ssize_t length = getline(&line, &size, stdin);
        /* Match config-file validation: never accept a prefix before NUL. */
        if (length < 0 || length > GALLERY_MAX_INPUT_LINE ||
            (size_t)length != strlen(line) ||
            set_field(c, fields[i], trim(line))) {
            fprintf(stderr, "Интерактивный ввод прерван или некорректен.\n");
            free(line);
            return -1;
        }
    }
    free(line);
    return 0;
}

/** @details Two getopt passes locate the config file before applying CLI
 * overrides. Final bounds and output-path checks run after optional prompts.
 */
int config_read(Config *config, int argc, char **argv) {
    *config = (Config){.capacity = 20,
                       .paintings = 7,
                       .painting_capacity = 3,
                       .visitors = 200,
                       .arrival_min = 1,
                       .arrival_max = 5,
                       .view_min = 10,
                       .view_max = 30,
                       .seed = 42,
                       .max_active = 10000,
                       .strategy = RANDOM};
    strcpy(config->log_path, "gallery.jsonl");
    strcpy(config->journal_path, "results.csv");
    const char *file = NULL;
    int option;
    opterr = 0;
    /* First pass locates the file; the second applies CLI after file and env.
     */
    while ((option = getopt_long(argc, argv, "", options, NULL)) != -1) {
        if (option == 'h') {
            help();
            return 1;
        }
        if (option == 'c')
            file = optarg;
        if (option == '?') {
            fprintf(stderr,
                    "Неизвестный/неполный аргумент; используйте --help.\n");
            return -1;
        }
    }
    if (optind != argc) {
        fprintf(stderr, "Лишний позиционный аргумент.\n");
        return -1;
    }
    if (file && read_file(config, file))
        return -1;
    for (int field = 0; field < FIELD_COUNT; ++field) {
        const char *value = getenv(keys[field]);
        if (value && set_field(config, field, value))
            return -1;
    }
    optind = 1;
    while ((option = getopt_long(argc, argv, "", options, NULL)) != -1) {
        if (option >= 1000 && option < 1000 + FIELD_COUNT) {
            int field = option - 1000;
            if (set_field(config, field, field >= UNLIMITED ? "1" : optarg))
                return -1;
        } else if (option == 'i')
            config->interactive = 1;
        else if (option == 'f')
            config->unlimited = 0;
    }
    if (config->interactive && interactive(config))
        return -1;
    if (config->capacity < 1 || config->capacity > 10000 ||
        config->paintings < 1 || config->paintings > GALLERY_MAX_PAINTINGS ||
        config->painting_capacity < 1 || config->painting_capacity > 10000 ||
        config->visitors > 100000 ||
        config->arrival_min > config->arrival_max ||
        config->arrival_max > 1000000 || config->view_min < 1 ||
        config->view_min > config->view_max || config->view_max > 1000000 ||
        config->delay_ms > 60000 || config->max_active < 1 ||
        config->max_active > 10000 ||
        (config->unlimited && config->arrival_max == 0)) {
        fprintf(stderr,
                "Параметры вне диапазона или интервалы некорректны; --help.\n");
        return -1;
    }
    if (strcmp(config->log_path, "-") != 0 &&
        !strcmp(config->log_path, config->journal_path)) {
        fprintf(stderr, "Журнал и лог должны быть разными файлами.\n");
        return -1;
    }
    return 0;
}
