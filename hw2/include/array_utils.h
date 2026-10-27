/** @file array_utils.h @brief Число элементов фиксированного массива. */
#ifndef GALLERY_ARRAY_UTILS_H
#define GALLERY_ARRAY_UTILS_H

/** @brief Получить число элементов фиксированного массива без его вычисления.
 * @param array Настоящий массив; указатель передавать нельзя.
 * @return Число элементов как size_t. Массивы переменной длины запрещены настройками сборки.
 */
#define GALLERY_ARRAY_COUNT(array) (sizeof(array) / sizeof((array)[0]))

#endif /* GALLERY_ARRAY_UTILS_H */
