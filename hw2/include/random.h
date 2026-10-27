/** @file random.h @brief Генераторы псевдослучайных чисел участников модели. */
#ifndef GALLERY_RANDOM_H
#define GALLERY_RANDOM_H

#include <stdint.h>

/** @brief Получить следующее число собственного генератора SplitMix64. @param state Изменяемое
 * состояние генератора.
 * @return Следующее 64-битное значение.
 */
uint64_t random_next(uint64_t *state);

/** @brief Равномерная ограниченная выборка с отбрасыванием для устранения смещения по модулю.
 * @param state Собственное состояние генератора.
 * @param bound Невключённая верхняя граница; должна быть положительной.
 * @return Целое число из диапазона [0, bound).
 */
uint64_t random_below(uint64_t *state, uint64_t bound);

/** @brief Получить начальное состояние собственного генератора участника.
 * @param seed Общее начальное значение.
 * @param participant Идентификатор участника.
 * @return Перемешанное начальное состояние; криптографическая стойкость не предполагается.
 */
uint64_t random_derive_seed(uint64_t seed, unsigned participant);

#endif /* GALLERY_RANDOM_H */
