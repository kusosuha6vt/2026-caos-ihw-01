/** @file simulation.h @brief Запуск и завершение многопоточной модели. */
#ifndef GALLERY_SIMULATION_H
#define GALLERY_SIMULATION_H

/** @brief Предварительное объявление конфигурации без её полей. */
typedef struct Config Config;

/** @brief Выполнить конечное моделирование и дождаться всех успешно созданных потоков.
 * @param config Проверенная конфигурация, доступная до возврата из функции без передачи владения.
 * @return 0 при завершении, 128+номер сигнала при плавной остановке, 1 при ошибке.
 */
int simulation_run(const Config *config);

#endif /* GALLERY_SIMULATION_H */
