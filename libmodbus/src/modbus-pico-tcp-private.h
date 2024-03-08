/*
 * Copyright © Gerhard Schiller 2024, <gerhard.schiller@pm.me>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * This file is based on the libmodbus-file "modbus-tcp-private.h"
 *
 * The original copyright notice is below.
 */
/*
 * Copyright © Stéphane Raimbault <stephane.raimbault@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef MODBUS_PICO_TCP_PRIVATE_H
#define MODBUS_PICO_TCP_PRIVATE_H

#define _MODBUS_TCP_HEADER_LENGTH     7
#define _MODBUS_TCP_PRESET_REQ_LENGTH 12
#define _MODBUS_TCP_PRESET_RSP_LENGTH 8

#define _MODBUS_TCP_CHECKSUM_LENGTH 0

#define BUF_SIZE (MODBUS_TCP_MAX_ADU_LENGTH + 1)

#define _WAIT_LOOP_INTERVAL_MS      1

/* The transaction ID must be placed on first position
 * to have a quick access not dependent of the TCP backend
 */
typedef struct _modbus_tcp {
    uint16_t            t_id;   // The transaction identifier is used to
                                // associate the future response with the request.
                                // This identifier is unique on each TCP connection.
    int                 port;   // TCP port
    char                ip[16]; // IP address
    struct tcp_pcb     *server_pcb;
    struct tcp_pcb     *client_pcb;
    uint8_t             buffer_sent[BUF_SIZE];
    uint8_t             buffer_recv[BUF_SIZE];
    int                 sent_len;
    int                 recv_len;
    bool                connected;
    bool                waitConnect;
    critical_section_t  cs;
} modbus_tcp_t;

/*
 * LWIP callbacks
 */
static err_t         tcp_server_accepted(
    void *arg, struct tcp_pcb *client_pcb, err_t err);

static err_t tcp_client_connected(
    void *arg, struct tcp_pcb *tpcb, err_t err);

err_t                tcp_connection_recved(
    void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);

static err_t         tcp_connection_sent(void *arg, struct tcp_pcb *tpcb, u16_t len);

static void          tcp_connection_err(void *arg, err_t err);

#if PICO_CYW43_ARCH_POLL
#define POLL_TIME_S 5
static err_t         tcp_connection_poll(void *arg, struct tcp_pcb *tpcb);
#endif


/*
 * LWIP helper functions
 */
static err_t tcp_connection_exit(void *arg);
static err_t tcp_connection_close(void *arg);
const char  *lwip_err_str(int err);


/*
 * LWIP specific modbus functions - internal
 */
static int          _modbus_tcp_connect(modbus_t *ctx);
static int          _modbus_tcp_select(
    modbus_t *ctx, fd_set *rset, struct timeval *tv, int length_to_read);
static ssize_t      _modbus_tcp_recv(
    modbus_t *ctx, uint8_t *rsp, int rsp_length);
static ssize_t      _modbus_tcp_send(
    modbus_t *ctx, const uint8_t *req, int req_length);
static void         _modbus_tcp_close(modbus_t *ctx);
static void         _modbus_tcp_free(modbus_t *ctx);
static int          _modbus_tcp_flush(modbus_t *ctx);


/*
 * TCP-Protocoll related functions
 */
static int _modbus_set_slave(modbus_t *ctx, int slave);
static int _modbus_tcp_build_request_basis(
    modbus_t *ctx, int function, int addr, int nb, uint8_t *req);
static int _modbus_tcp_build_response_basis(sft_t *sft, uint8_t *rsp);
static int _modbus_tcp_prepare_response_tid(
    const uint8_t *req, int *req_length);
static int _modbus_tcp_send_msg_pre(uint8_t *req, int req_length);
static int _modbus_tcp_receive(modbus_t *ctx, uint8_t *req);
static int _modbus_tcp_check_integrity(
    modbus_t *ctx, uint8_t *msg, const int msg_length);
static int _modbus_tcp_pre_check_confirmation(
    modbus_t *ctx, const uint8_t *req, const uint8_t *rsp, int rsp_length);

#endif /* MODBUS_PICO_TCP_PRIVATE_H */
