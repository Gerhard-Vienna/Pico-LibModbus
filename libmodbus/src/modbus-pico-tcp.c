/*
 * Copyright © Gerhard Schiller 2024, <gerhard.schiller@pm.me>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * This file is based on the libmodbus-file "modbus-tcp.c"
 *
 * The original copyright notice is below.
 */
/*
 * Copyright © Stéphane Raimbault <stephane.raimbault@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/sync.h"

#include "lwipopts.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"

#include "modbus-private.h"

#include "modbus-pico-tcp-private.h"
#include "modbus-pico-tcp.h"

// #define DEBUG_printf(...) printf(__VA_ARGS__)
#define DEBUG_printf(...)

/*
 * LWIP callback functions
 */

// called when a client connects
static err_t tcp_server_accepted(void *arg, struct tcp_pcb *client_pcb, err_t err)
{
    DEBUG_printf("+++ tcp_server_accepted()\n");

    modbus_t *ctx = (modbus_t *) arg;
    modbus_tcp_t *ctx_tcp  = (modbus_tcp_t *) ctx->backend_data;;

    if (err != ERR_OK) {
        if (ctx->debug)
            printf("\tFailure in accept: %s\n", lwip_err_str(err));
        tcp_connection_exit(arg);
        return ERR_VAL;
    }
    if (client_pcb == NULL) {
        if (ctx->debug)
            printf("\tFailure in accept: client_pcb == NULL\n");
        tcp_connection_exit(arg);
        return ERR_VAL;
    }

    ctx_tcp->client_pcb = client_pcb;
    tcp_arg(client_pcb, ctx);
    tcp_sent(client_pcb, tcp_connection_sent);
    tcp_recv(client_pcb, tcp_connection_recved);
    #if PICO_CYW43_ARCH_POLL
    tcp_poll(client_pcb, tcp_connection_poll, POLL_TIME_S * 2);
    #endif
    tcp_err(client_pcb, tcp_connection_err);

    DEBUG_printf("--- tcp_server_accepted(): Client connected\n");

    ctx_tcp->connected = true;
    return ERR_OK;
}

// called when a pcb is connected to the remote side after initiating a
// connection attempt by calling tcp_connect().
static err_t tcp_client_connected(void *arg, struct tcp_pcb *tpcb, err_t err) {
    /* err	An unused error code, always ERR_OK currently ;-)
     * Note:
     * When a connection attempt fails, the error callback is currently called!
     * (Instead of THIS callback!)
     */

    DEBUG_printf("+++ tcp_client_connected()\n");
    modbus_t *ctx = (modbus_t *) arg;
    modbus_tcp_t *ctx_tcp  = (modbus_tcp_t *) ctx->backend_data;;
    //     if (err != ERR_OK) {
    //         printf("connect failed %d\n", err);
    //         state->connected = false;
    //         state->waitConnect = false;
    //         return err;
    //     }
    ctx_tcp->connected = true;
    ctx_tcp->waitConnect = false;
    return ERR_OK;
}

// called when a fatal error has occurred on the connection
static void tcp_connection_err(void *arg, err_t err) {
    DEBUG_printf("+++ tcp_connection_err()\n");
    modbus_t *ctx = (modbus_t *) arg;
    modbus_tcp_t *ctx_tcp  = (modbus_tcp_t *) ctx->backend_data;;

    ctx_tcp->waitConnect = false;
    ctx_tcp->connected = false;
    errno = ECONNRESET;

    // this is thrown if a connection could not established by tcp_connect()
    // (most likely because the remote is down)
    if(err == ERR_RST || err == ERR_ABRT){
        ctx_tcp->connected = false;
    }
    else{
        if (ctx->debug)
            printf("tcp_connection_err(): %s (%d)\n", lwip_err_str(err), err);
//         if (err != ERR_ABRT) {
//             TODO How to handle this?
            tcp_connection_exit(arg);
//         }
    }
}

#if PICO_CYW43_ARCH_POLL
static err_t tcp_connection_poll(void *arg, struct tcp_pcb *tpcb) {
    DEBUG_printf("tcp_connection_poll\n");
    return ERR_OK;
}
#endif

// called when sent data has been acknowledged by the remote side.
static err_t tcp_connection_sent(void *arg, struct tcp_pcb *tpcb, u16_t len) {
    DEBUG_printf("+++ tcp_connection_sent(): sent %u bytes\n", len);

    modbus_t *ctx = (modbus_t *) arg;
    modbus_tcp_t *ctx_tcp  = (modbus_tcp_t *) ctx->backend_data;;

    ctx_tcp->sent_len = len;
    return ERR_OK;
}

// called when data has been received.
err_t tcp_connection_recved(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    DEBUG_printf("+++ tcp_connection_recved()\n");

    modbus_t *ctx = (modbus_t *) arg;
    modbus_tcp_t *ctx_tcp  = (modbus_tcp_t *) ctx->backend_data;;

    if (!p) {
        DEBUG_printf("\tp = NULL, Error: %s\n", lwip_err_str(err));
        err = tcp_close(tpcb);
        DEBUG_printf("\ttcp_close returns: %s\n", lwip_err_str(err));
        ctx_tcp->connected = false;
        return ERR_OK;
    }

//     TODO handle this gracefully...
    if (err != ERR_OK) {
        if (ctx->debug)
            printf("\tERROR: %s (%d)\n", lwip_err_str(err), err);
        return tcp_connection_exit(arg);
    }

// this method is callback from lwIP, so cyw43_arch_lwip_begin is not
// required, however you can use this method to cause an assertion
// in debug mode, if this method is called when cyw43_arch_lwip_begin IS needed
    cyw43_arch_lwip_check();

// Receive the buffer
    if (p->tot_len > 0) {
        const uint16_t buffer_left = BUF_SIZE - ctx_tcp->recv_len;
        ctx_tcp->recv_len += pbuf_copy_partial(
            p, ctx_tcp->buffer_recv + ctx_tcp->recv_len,
            p->tot_len > buffer_left ? buffer_left : p->tot_len, 0);

        DEBUG_printf("\trecv_len: %d, tot_len: %d\n", ctx_tcp->recv_len, p->tot_len);
        tcp_recved(tpcb, p->tot_len);
    }
    pbuf_free(p);
    return ERR_OK;
}

/*
 * LWIP helper functions
 */
static err_t tcp_connection_exit(void *arg)
{
    DEBUG_printf("+++ tcp_connection_exit()\n");
    return tcp_connection_close(arg);
}

static err_t tcp_connection_close(void *arg)
{
    DEBUG_printf("+++ tcp_connection_close()\n");

    modbus_t *ctx = (modbus_t *) arg;
    modbus_tcp_t *ctx_tcp  = (modbus_tcp_t *) ctx->backend_data;;
    ctx_tcp->connected = false;

    err_t err = ERR_OK;
    if (ctx_tcp->client_pcb != NULL) {
        tcp_arg(ctx_tcp->client_pcb, NULL);
        tcp_poll(ctx_tcp->client_pcb, NULL, 0);
        tcp_sent(ctx_tcp->client_pcb, NULL);
        tcp_recv(ctx_tcp->client_pcb, NULL);
        tcp_err(ctx_tcp->client_pcb, NULL);
        err = tcp_close(ctx_tcp->client_pcb);
        if (err != ERR_OK) {
            if (ctx->debug)
                printf("\tclose failed %d, calling abort\n", err);
            tcp_abort(ctx_tcp->client_pcb);
            err = ERR_ABRT;
        }
        ctx_tcp->client_pcb = NULL;
    }
    if (ctx_tcp->server_pcb) {
        tcp_arg(ctx_tcp->server_pcb, NULL);
        err = tcp_close(ctx_tcp->server_pcb);
        if (err != ERR_OK) {
            if (ctx->debug)
                printf("\tclose failed %d, calling abort\n", err);
            tcp_abort(ctx_tcp->client_pcb);
            err = ERR_ABRT;
        }
        ctx_tcp->server_pcb = NULL;
    }
    return err;
}

//lwIP error codes
const char * err_names[] = {
    "ERR_OK",       // 0
    "ERR_MEM",      // -1
    "ERR_BUF",      // -2
    "ERR_TIMEOUT",  // -3
    "ERR_RTE",      // -4
    "ERR_INPROGRESS",// -5
    "ERR_VAL",      // -6
    "ERR_WOULDBLOCK",// -7
    "ERR_USE",      // -8
    "ERR_ALREADY",  // -9
    "ERR_ISCONN",   // -10
    "ERR_CONN",     // -11
    "ERR_IF",       // -12
    "ERR_ABRT",     // -13
    "ERR_RST",      // -14
    "ERR_CLSD",     // -15
    "ERR_ARG"       // -16
};

const char *lwip_err_str(int err)
{
    if(err > ERR_OK || err < ERR_ARG)
        return("unkown error code");
    else
        return(err_names[-err]);
}

/*
 * LWIP specific modbus functions
 */
const modbus_backend_t _modbus_tcp_backend = {
    _MODBUS_BACKEND_TYPE_TCP,
    _MODBUS_TCP_HEADER_LENGTH,
    _MODBUS_TCP_CHECKSUM_LENGTH,
    MODBUS_TCP_MAX_ADU_LENGTH,
    _modbus_set_slave,
    _modbus_tcp_build_request_basis,
    _modbus_tcp_build_response_basis,
    _modbus_tcp_prepare_response_tid,
    _modbus_tcp_send_msg_pre,
    _modbus_tcp_send,
    _modbus_tcp_receive,
    _modbus_tcp_recv,
    _modbus_tcp_check_integrity,
    _modbus_tcp_pre_check_confirmation,
    _modbus_tcp_connect,
    modbus_tcp_is_connected,
    _modbus_tcp_close,
    _modbus_tcp_flush,
    _modbus_tcp_select,
    _modbus_tcp_free,
    modbus_tcp_mapping_lock,
    modbus_tcp_mapping_unlock
};

modbus_t *modbus_new_tcp(const char *ip, int port)
{
    DEBUG_printf("+++ modbus_new_tcp()\n");
    modbus_t *ctx;
    modbus_tcp_t *ctx_tcp;
    size_t dest_size;
    size_t ret_size;

    ctx = (modbus_t *) malloc(sizeof(modbus_t));
    if (ctx == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    _modbus_init_common(ctx);

    /* Could be changed after to reach a remote serial Modbus device */
    ctx->slave = MODBUS_TCP_SLAVE;

    ctx->backend = &_modbus_tcp_backend;

    ctx->backend_data = (modbus_tcp_t *) malloc(sizeof(modbus_tcp_t));
    if (ctx->backend_data == NULL) {
        modbus_free(ctx);
        errno = ENOMEM;
        return NULL;
    }
    ctx_tcp = (modbus_tcp_t *) ctx->backend_data;
    memset(ctx_tcp, 0, sizeof(modbus_tcp_t));

    if (ip != NULL) {
        dest_size = sizeof(char) * 16;
        ret_size = strlcpy(ctx_tcp->ip, ip, dest_size);
        if (ret_size == 0) {
            if (ctx->debug)
                printf("\tThe IP string is empty\n");
            modbus_free(ctx);
            errno = EINVAL;
            return NULL;
        }

        if (ret_size >= dest_size) {
            if (ctx->debug)
                printf("\tThe IP string has been truncated\n");
            modbus_free(ctx);
            errno = EINVAL;
            return NULL;
        }
    } else {
        ctx_tcp->ip[0] = '0';
    }

    ctx_tcp->port = port;
    ctx_tcp->connected = false;

    critical_section_init(&(ctx_tcp->cs));

    return ctx;
}

/* Listens for request from modbus master in TCP */
int modbus_tcp_listen(modbus_t *ctx, int nb_connection)
{
    DEBUG_printf("+++ modbus_tcp_listen()\n");

    modbus_tcp_t *ctx_tcp;
    ctx_tcp = (modbus_tcp_t *) ctx->backend_data;

    if (ctx->debug)
        printf("\tStarting server at %s on port %u\n",
                 ip4addr_ntoa(netif_ip4_addr(netif_list)),
                 ctx_tcp->port);

    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (!pcb) {
        if (ctx->debug)
            printf("\tfailed to create pcb\n");
        return -1;
    }

    err_t err = tcp_bind(pcb, NULL, ctx_tcp->port);
    if (err) {
        if (ctx->debug)
            printf("\tfailed to bind to port %d\n", ctx_tcp->port);
        return false;
    }

    ctx_tcp->server_pcb = tcp_listen_with_backlog(pcb, nb_connection);
    if (!ctx_tcp->server_pcb) {
        if (ctx->debug)
            printf("\tfailed to listen\n");
        if (pcb) {
            tcp_close(pcb);
        }
        return -1;
    }

    tcp_arg(ctx_tcp->server_pcb, ctx);
    tcp_accept(ctx_tcp->server_pcb, tcp_server_accepted);

    return 1;
}

// called from modbus.c Waits until a client connects
int modbus_tcp_accept(modbus_t *ctx, int *s)
{
    DEBUG_printf("+++ modbus_tcp_accept()\n");

    modbus_tcp_t *ctx_tcp;
    ctx_tcp = (modbus_tcp_t *) ctx->backend_data;
    //     TCP_SERVER_T *state = (TCP_SERVER_T*)ctx_tcp->state;

    while(!ctx_tcp->connected){
        sleep_ms(_WAIT_LOOP_INTERVAL_MS);
    }

    DEBUG_printf("--- modbus_tcp_accept()\n");
    return 1;
}

/* Establishes a modbus TCP connection with a Modbus server.
 * Returns -1 on error
 */
static int _modbus_tcp_connect(modbus_t *ctx)
{
    DEBUG_printf("+++ _modbus_tcp_connect()\n");

    modbus_tcp_t *ctx_tcp;
    ctx_tcp = (modbus_tcp_t *) ctx->backend_data;
    DEBUG_printf("\tConnecting to %s port %u\n", ctx_tcp->ip, ctx_tcp->port);


    ip_addr_t remote_addr;
    ip4addr_aton(ctx_tcp->ip, &remote_addr);

    struct tcp_pcb *client_pcb;
    client_pcb = tcp_new_ip_type(IP_GET_TYPE(&remote_addr));
    if (!client_pcb) {
        DEBUG_printf("\tfailed to create pcb\n");
        return false;
    }

    ctx_tcp->client_pcb = client_pcb;
    tcp_arg(client_pcb, ctx);
    tcp_sent(client_pcb, tcp_connection_sent);
    tcp_recv(client_pcb, tcp_connection_recved);
#if PICO_CYW43_ARCH_POLL
    tcp_poll(client_pcb, tcp_connection_poll, POLL_TIME_S * 2);
#endif
    tcp_err(client_pcb, tcp_connection_err);

    cyw43_arch_lwip_begin();
    ctx_tcp->waitConnect = true;
    err_t err = tcp_connect(
        ctx_tcp->client_pcb, &remote_addr, ctx_tcp->port, tcp_client_connected);
    if (ctx->debug)
        printf("\tResult from tcp_connect(): %s (%d)\n", lwip_err_str(err), err);

    cyw43_arch_lwip_end();

    while(ctx_tcp->waitConnect == true){
        sleep_ms(_WAIT_LOOP_INTERVAL_MS);
    }
    if(ctx_tcp->connected){
        if (ctx->debug)
            printf("\tConnect: OK\n");
        return 0;
    }
    else{
        if (ctx->debug)
            printf("\tConnect: FAILED\n");
        return -1;
    }

}

unsigned int modbus_tcp_is_connected(modbus_t *ctx)
{
    DEBUG_printf("+++ modbus_tcp_is_connected()\n");

    modbus_tcp_t *ctx_tcp;
    ctx_tcp = (modbus_tcp_t *) ctx->backend_data;

    return ctx_tcp->connected;
}

static int
_modbus_tcp_select(modbus_t *ctx, fd_set *rset, struct timeval *tv, int length_to_read)
{
    modbus_tcp_t *ctx_tcp;
    int timeout_ms = 0;

    DEBUG_printf("+++ _modbus_tcp_select(), timeout: ");
    if(tv){
        timeout_ms =  tv->tv_sec * 1000;
        timeout_ms += tv->tv_usec / 1000;
        DEBUG_printf("%d ms\n", timeout_ms);
    }
    else{
        DEBUG_printf("none\n");
    }
    ctx_tcp = (modbus_tcp_t *) ctx->backend_data;

    while(ctx_tcp->recv_len == 0 && ctx_tcp->connected){
        if(tv){
            if(timeout_ms == 0){
                DEBUG_printf("--- _modbus_tcp_select(): Timeout!\n");
                errno = ETIMEDOUT;
                return -1;
            }
            timeout_ms -= _WAIT_LOOP_INTERVAL_MS;
        }
        sleep_ms(_WAIT_LOOP_INTERVAL_MS);
    }
    DEBUG_printf("--- _modbus_tcp_select()\n");
    return 1;
}

static ssize_t _modbus_tcp_recv(modbus_t *ctx, uint8_t *rsp, int rsp_length)
{
    modbus_tcp_t *ctx_tcp;
    uint16_t numBytes;

    DEBUG_printf("+++ _modbus_tcp_recv(%d)\n", rsp_length);

    ctx_tcp = (modbus_tcp_t *) ctx->backend_data;
    if(ctx_tcp->recv_len == 0){
        if(ctx_tcp->connected){
            printf("\tthis should never happen...\n");
        }
        if (ctx->debug)
            printf("\tremote closed connection\n");
        return -1;
    }

    numBytes = rsp_length < ctx_tcp->recv_len ? rsp_length : ctx_tcp->recv_len;
    memcpy(rsp, ctx_tcp->buffer_recv, numBytes);
    memmove(ctx_tcp->buffer_recv, ctx_tcp->buffer_recv + numBytes, ctx_tcp->recv_len - numBytes);

    ctx_tcp->recv_len -= numBytes;
    if (ctx->debug)
        printf("\t<Received %d byte(s) from remote>\n", numBytes);

    return numBytes;

}

static ssize_t _modbus_tcp_send(modbus_t *ctx, const uint8_t *req, int req_length)
{
    DEBUG_printf("+++ _modbus_tcp_send()\n");
    modbus_tcp_t *ctx_tcp;
    ctx_tcp = (modbus_tcp_t *) ctx->backend_data;

    if(!ctx_tcp->connected){
        if (ctx->debug)
            printf("\tNot sending %d byte(s), connection is down\n", req_length);
        errno = ECONNRESET;
        return(-1);
    }

    ctx_tcp->sent_len = 0;
    errno = 0;

    if (ctx->debug)
        printf("\t[Writing %d byte(s) to remote]\n", req_length);

    cyw43_arch_lwip_begin();
    struct tcp_pcb *tpcb = ctx_tcp->client_pcb;
    err_t err = tcp_write(tpcb, req, req_length, TCP_WRITE_FLAG_COPY);
    if (err != ERR_OK) {
        if (ctx->debug)
            printf("\tFailed to write data: %s (%d)\n", lwip_strerr(err), err);
        errno = EPIPE;
        ctx_tcp->connected = false;
        return -1;
    }
    cyw43_arch_lwip_end();

    // wait for sent-callback and return the actual count!!!!
    while(ctx_tcp->sent_len == 0){
        if(!ctx_tcp->connected){
            if (ctx->debug)
                printf("\tFailed to write data: connection is down\n");
            errno = EPIPE;
            return -1;
        }
        sleep_ms(_WAIT_LOOP_INTERVAL_MS);
    }
    DEBUG_printf("--- _modbus_tcp_send(): %d bytes acknowleged\n", ctx_tcp->sent_len);

    int sent_len = ctx_tcp->sent_len;
    ctx_tcp->sent_len = 0;

    return sent_len;
}

/* Closes the network connection and socket in TCP mode */
static void _modbus_tcp_close(modbus_t *ctx)
{
    DEBUG_printf("+++ _modbus_tcp_close()\n");
    tcp_connection_exit(ctx);
}

static void _modbus_tcp_free(modbus_t *ctx)
{
    DEBUG_printf("+++ _modbus_tcp_free()\n");
    if (ctx->backend_data) {
        free(ctx->backend_data);
    }
    free(ctx);
}

static int _modbus_tcp_flush(modbus_t *ctx)
{
    DEBUG_printf("+++ _modbus_tcp_flush()\n");

    modbus_tcp_t *ctx_tcp  = (modbus_tcp_t *) ctx->backend_data;
    int recv_len = ctx_tcp->recv_len;

    ctx_tcp->recv_len = 0;
    ctx_tcp->sent_len = 0;
    return recv_len;
}

/*
 * TCP-Protocoll related functions
 */
static int _modbus_set_slave(modbus_t *ctx, int slave)
{
    DEBUG_printf("+++ _modbus_set_slave\n");

    int max_slave = (ctx->quirks & MODBUS_QUIRK_MAX_SLAVE) ? 255 : 247;

    /* Broadcast address is 0 (MODBUS_BROADCAST_ADDRESS) */
    if (slave >= 0 && slave <= max_slave) {
        ctx->slave = slave;
    } else if (slave == MODBUS_TCP_SLAVE) {
        /* The special value MODBUS_TCP_SLAVE (0xFF) can be used in TCP mode to
         * restore the default value. */
        ctx->slave = slave;
    } else {
        errno = EINVAL;
        return -1;
    }

    return 0;
}

/* Builds a TCP request header */
static int _modbus_tcp_build_request_basis(
    modbus_t *ctx, int function, int addr, int nb, uint8_t *req)
{
    modbus_tcp_t *ctx_tcp = ctx->backend_data;

    /* Increase transaction ID */
    if (ctx_tcp->t_id < UINT16_MAX)
        ctx_tcp->t_id++;
    else
        ctx_tcp->t_id = 0;
    req[0] = ctx_tcp->t_id >> 8;
    req[1] = ctx_tcp->t_id & 0x00ff;

    /* Protocol Modbus */
    req[2] = 0;
    req[3] = 0;

    /* Length will be defined later by set_req_length_tcp at offsets 4
     *       and 5 */

    req[6] = ctx->slave;
    req[7] = function;
    req[8] = addr >> 8;
    req[9] = addr & 0x00ff;
    req[10] = nb >> 8;
    req[11] = nb & 0x00ff;

    return _MODBUS_TCP_PRESET_REQ_LENGTH;
}

/* Builds a TCP response header */
static int _modbus_tcp_build_response_basis(sft_t *sft, uint8_t *rsp)
{
    /* Extract from MODBUS Messaging on TCP/IP Implementation
     *       Guide V1.0b (page 23/46):
     *       The transaction identifier is used to associate the future
     *       response with the request. */
    rsp[0] = sft->t_id >> 8;
    rsp[1] = sft->t_id & 0x00ff;

    /* Protocol Modbus */
    rsp[2] = 0;
    rsp[3] = 0;

    /* Length will be set later by send_msg (4 and 5) */

    /* The slave ID is copied from the indication */
    rsp[6] = sft->slave;
    rsp[7] = sft->function;

    return _MODBUS_TCP_PRESET_RSP_LENGTH;
}

static int _modbus_tcp_prepare_response_tid(const uint8_t *req, int *req_length)
{
    return (req[0] << 8) + req[1];
}

static int _modbus_tcp_send_msg_pre(uint8_t *req, int req_length)
{
    /* Subtract the header length to the message length */
    int mbap_length = req_length - 6;

    req[4] = mbap_length >> 8;
    req[5] = mbap_length & 0x00FF;

    return req_length;
}

static int _modbus_tcp_receive(modbus_t *ctx, uint8_t *req)
{
    return _modbus_receive_msg(ctx, req, MSG_INDICATION);
}

static int _modbus_tcp_check_integrity(modbus_t *ctx, uint8_t *msg, const int msg_length)
{
    return msg_length;
}

static int _modbus_tcp_pre_check_confirmation(modbus_t *ctx,
                                              const uint8_t *req,
                                              const uint8_t *rsp,
                                              int rsp_length)
{
    unsigned int protocol_id;
    /* Check transaction ID */
    if (req[0] != rsp[0] || req[1] != rsp[1]) {
        if (ctx->debug) {
            fprintf(stderr,
                    "Invalid transaction ID received 0x%X (not 0x%X)\n",
                    (rsp[0] << 8) + rsp[1],
                    (req[0] << 8) + req[1]);
        }
        errno = EMBBADDATA;
        return -1;
    }

    /* Check protocol ID */
    protocol_id = (rsp[2] << 8) + rsp[3];
    if (protocol_id != 0x0) {
        if (ctx->debug) {
            fprintf(stderr, "Invalid protocol ID received 0x%X (not 0x0)\n", protocol_id);
        }
        errno = EMBBADDATA;
        return -1;
    }

    return 0;
}

bool modbus_tcp_message(modbus_t *ctx, const uint8_t *req, modbus_message_t *msg)
{
    unsigned int offset;

    offset = ctx->backend->header_length;
    msg->code  = req[offset];

    switch (msg->code) {
//      Theese request do not modify the state of the modbus device
//         case MODBUS_FC_READ_COILS:
//         case MODBUS_FC_READ_DISCRETE_INPUTS:
//             msg->addr  = (req[offset + 1] << 8) + req[offset + 2];
//             msg->count = (req[offset + 3] << 8) + req[offset + 4];
//             break;
//
//         case MODBUS_FC_READ_HOLDING_REGISTERS:
//         case MODBUS_FC_READ_INPUT_REGISTERS:
//             msg->addr  = (req[offset + 1] << 8) + req[offset + 2];
//             msg->count = (req[offset + 3] << 8) + req[offset + 4];
//             break;
//
        case MODBUS_FC_WRITE_SINGLE_COIL:
            msg->addr  = (req[offset + 1] << 8) + req[offset + 2];
            msg->count = 1;
            break;

        case MODBUS_FC_WRITE_SINGLE_REGISTER:
            msg->addr  = (req[offset + 1] << 8) + req[offset + 2];
            msg->count = 1;
            break;

        case MODBUS_FC_WRITE_MULTIPLE_COILS:
            msg->addr  = (req[offset + 1] << 8) + req[offset + 2];
            msg->count = (req[offset + 3] << 8) + req[offset + 4];
            break;

        case MODBUS_FC_WRITE_MULTIPLE_REGISTERS:
            msg->addr  = (req[offset + 1] << 8) + req[offset + 2];
            msg->count = (req[offset + 3] << 8) + req[offset + 4];
            break;

        case MODBUS_FC_WRITE_AND_READ_REGISTERS:
            msg->addr  = (req[offset + 5] << 8) + req[offset + 6];
            msg->count = (req[offset + 7] << 8) + req[offset + 8];
            break;

        default:
           return false;
    }
    return true;
}

void modbus_tcp_mapping_lock(modbus_t *ctx)
{
    modbus_tcp_t *ctx_tcp = ctx->backend_data;
    critical_section_enter_blocking(&(ctx_tcp->cs));
}

void modbus_tcp_mapping_unlock(modbus_t *ctx)
{
    modbus_tcp_t *ctx_tcp = ctx->backend_data;
    critical_section_exit(&(ctx_tcp->cs));
}

int modbus_tcp_get_error(void)
{
    return errno;
}

bool modbus_get_debug(modbus_t *ctx)
{
    return ctx->debug;
}
