/*
 * SPDX-FileCopyrightText: 2026 flydigi-receiver
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#ifndef BT_APP_BT_STACK_H
#define BT_APP_BT_STACK_H

#include "esp_err.h"

/* Bring up BR/EDR (Bluedroid, BTDM mode) + SSP/security params + scan mode.
 * Does NOT register the GAP callback nor start discovery (caller does). */
esp_err_t bt_stack_start(void);

#endif /* BT_APP_BT_STACK_H */
