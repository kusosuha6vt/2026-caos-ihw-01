/** @file exit_status.h @brief Общие коды завершения приложения. */
#ifndef GALLERY_EXIT_STATUS_H
#define GALLERY_EXIT_STATUS_H

/** @brief Дополнительные коды завершения приложения. */
enum {
    GALLERY_EXIT_CONFIG_ERROR = 2, /**< Некорректные параметры или конфигурация. */
    GALLERY_SIGNAL_EXIT_BASE = 128 /**< Основа кода завершения по сигналу. */
};

#endif /* GALLERY_EXIT_STATUS_H */
