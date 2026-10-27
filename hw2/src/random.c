/** @file random.c @brief Собственный воспроизводимый некриптографический генератор каждого
 * участника. */
#include "random.h"
#include <assert.h>

/** @brief Параметры алгоритма SplitMix64. */
enum {
    MIX_SHIFT_FIRST = 30, /**< Первый сдвиг перемешивания. */
    MIX_SHIFT_SECOND = 27, /**< Второй сдвиг перемешивания. */
    MIX_SHIFT_FINAL = 31 /**< Заключительный сдвиг перемешивания. */
};
/** @brief Приращение состояния SplitMix64. */
static const uint64_t MIX_INCREMENT = UINT64_C(0x9e3779b97f4a7c15);
/** @brief Первый множитель перемешивания SplitMix64. */
static const uint64_t MIX_MULTIPLIER_FIRST = UINT64_C(0xbf58476d1ce4e5b9);
/** @brief Второй множитель перемешивания SplitMix64. */
static const uint64_t MIX_MULTIPLIER_SECOND = UINT64_C(0x94d049bb133111eb);

uint64_t random_next(uint64_t *state) {
    uint64_t value = (*state += MIX_INCREMENT);
    value = (value ^ (value >> MIX_SHIFT_FIRST)) * MIX_MULTIPLIER_FIRST;
    value = (value ^ (value >> MIX_SHIFT_SECOND)) * MIX_MULTIPLIER_SECOND;
    return value ^ (value >> MIX_SHIFT_FINAL);
}

uint64_t random_below(uint64_t *state, uint64_t bound) {
    assert(bound > 0);
    uint64_t threshold = (uint64_t)(-bound) % bound;
    uint64_t value;
    do {
        value = random_next(state);
    } while (value < threshold);
    return value % bound;
}

uint64_t random_derive_seed(uint64_t seed, unsigned participant) {
    uint64_t state = seed ^ (MIX_INCREMENT * participant);
    return random_next(&state);
}
