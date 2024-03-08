/*
 * Copyright © Gerhard Schiller 2024, <gerhard.schiller@pm.me>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * This file is based on the libmodbus-file "modbus-tcp.h"
 *
 * The original copyright notice is below.
 */
/*
 * Copyright © Stéphane Raimbault <stephane.raimbault@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef MODBUS_PICO_TCP_H
#define MODBUS_PICO_TCP_H

#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include <stdbool.h>

#define MODBUS_TCP_DEFAULT_PORT 502
#define MODBUS_TCP_SLAVE        0xFF

/* Modbus_Application_Protocol_V1_1b.pdf Chapter 4 Section 1 Page 5
 * TCP MODBUS ADU = 253 bytes + MBAP (7 bytes) = 260 bytes
 */
#define MODBUS_TCP_MAX_ADU_LENGTH 260


typedef struct _modbus_message_t {
    uint8_t     code;
    uint16_t    addr;
    uint16_t    count;
}modbus_message_t;

MODBUS_API modbus_t *modbus_new_tcp(const char *ip_address, int port);
MODBUS_API int modbus_tcp_listen(modbus_t *ctx, int nb_connection);
MODBUS_API int modbus_tcp_accept(modbus_t *ctx, int *s);
MODBUS_API unsigned int modbus_tcp_is_connected(modbus_t *ctx);
bool modbus_tcp_message(modbus_t *ctx, const uint8_t *req, modbus_message_t *msg);
void modbus_tcp_mapping_lock(modbus_t *ctx);
void modbus_tcp_mapping_unlock(modbus_t *ctx);
int modbus_tcp_get_error(void);
bool modbus_get_debug(modbus_t *ctx);

#endif /* MODBUS_PICO_TCP_H */
