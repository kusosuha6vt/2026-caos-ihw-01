/** @file main.c @brief Точка входа командной строки; конфигурация освобождается после
 * моделирования. */
#include "config.h"
#include "exit_status.h"
#include "simulation.h"
#include <stdlib.h>

/** @brief Разобрать аргументы, выполнить моделирование и освободить конфигурацию.
 * @param argc Число аргументов. @param argv Аргументы. @return Код завершения процесса.
 */
int main(int argc, char **argv) {
    Config config;
    int parsed = config_parse(argc, argv, &config);
    if (parsed != 0) {
        config_destroy(&config);
        return parsed > 0 ? EXIT_SUCCESS : GALLERY_EXIT_CONFIG_ERROR;
    }
    int result = simulation_run(&config);
    config_destroy(&config);
    return result;
}
