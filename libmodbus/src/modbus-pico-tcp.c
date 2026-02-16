/*
 * Copyright © Gerhard Schiller 2024, 2025, <gerhard.schiller@pm.me>
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
#include "pico/multicore.h"
#include "pico/sync.h"

#include "lwipopts.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"

#include "modbus-private.h"
#include "modbus.h"

#include "modbus-pico-tcp-private.h"
#include "modbus-pico-tcp.h"

#ifdef PICO_TCP_DEBUG
    #define DEBUG_printf(...) printf(__VA_ARGS__)
#else
    #define DEBUG_printf(...)
#endif

#ifdef PICO_TCP_ASSERT
    #include "pico/bootrom.h"
    #define PICO_ASSERT(message, assertion) \
    if (!(assertion)) { \
        printf("Assertion \"%s\" failed at line %d in %s\n", \
                message, __LINE__, __FILE__); \
        reset_usb_boot(0, 0); \
    }
#else  /* PICO_TCP_ASSERT */
    #define PICO_ASSERT(message, assertion)
#endif /* PICO_TCP_ASSERT */

// SERVER and CLIENT
// Array to hold active connections
static tcp_connection *peers[MAX_PEERS];

static int *_debug;
static int *_error_recovery;

/*************************************************************
 * The modified functions from modbus-tcp.c
 *************************************************************/
// SERVER
static int _modbus_set_slave(modbus_t *ctx, int slave)
{
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
    tcp_connection *peer = peers[ctx->s];

    /* Increase transaction ID */
    if (peer->t_id < UINT16_MAX)
        peer->t_id++;
    else
        peer->t_id = 0;
    req[0] = peer->t_id >> 8;
    req[1] = peer->t_id & 0x00ff;

    /* Protocol Modbus */
    req[2] = 0;
    req[3] = 0;

    /* Length will be defined later by set_req_length_tcp at offsets 4
       and 5 */

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
       Guide V1.0b (page 23/46):
       The transaction identifier is used to associate the future
       response with the request. */
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

static int _modbus_tcp_get_response_tid(const uint8_t *req)
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

// SERVER AND CLIENT -> aka "peer"
// Send data to peer, mimics the send() system-call on linux
// errno used: EBADF, ECONNRESET
// errno from linux send() UNUSED: EPIPE
static ssize_t _modbus_tcp_send(modbus_t *ctx, const uint8_t *req, int req_length)
{
    PICO_ASSERT("ctx != NULL", ctx != NULL);
    if(ctx->s < 0 && ctx->s >= MAX_PEERS){
        if(*_debug){
            printf("Inst %d, _modbus_tcp_send(): invalid connection\n",
                   ctx->s);
        }
        errno = ECONNRESET;
        return -1;
    }
    if(peers[ctx->s] == NULL){
        if(*_debug){
            printf("Inst %d, _modbus_tcp_send(): non-existant connection\n",
                   ctx->s);
        }
        errno = ECONNRESET;
        return -1;
    }

    if(peers[ctx->s]->pcb == NULL || peers[ctx->s]->error == ERR_CLSD){
        if(*_debug){
            printf("Inst %d, _modbus_tcp_send(): closed connection\n",
                   ctx->s);
        }
        errno = ECONNRESET;
        return -1;
    }

    if(peers[ctx->s]->error == ERR_RST || peers[ctx->s]->error == ERR_ABRT){
       errno = ECONNRESET;
       return -1;
    }

    tcp_connection *peer = peers[ctx->s];

    int rc;
    DEBUG_printf("Inst %d, _modbus_tcp_send()\n", peer->instance);

    peer->send_len = 0;
    if(*_debug && peer->recv_len > 0){
        printf("Inst %d, _modbus_tcp_send(), unexpected %d bytes in recv-buffer\n",
               peer->instance,  peer->recv_len);
    }
    peer->recv_len = 0;

    cyw43_arch_lwip_begin();
    rc = tcp_write(peer->pcb, req, req_length, TCP_WRITE_FLAG_COPY);
    cyw43_arch_lwip_end();

    if(rc != ERR_OK){
        if(*_debug){
            printf("Inst %d, _modbus_tcp_send(): tcp_write failed, \"%s\"\n",
                   peer->instance, err_txt(rc));
        }
        _modbus_tcp_close(ctx);

        peer->error = rc;
        errno = ECONNRESET;
        return -1;
    }
    DEBUG_printf("Inst %d, _modbus_tcp_send(): tcp_write OK\n",
                 peer->instance);

    cyw43_arch_lwip_begin();
    rc = tcp_output(peer->pcb);
    cyw43_arch_lwip_end();
    if(rc != ERR_OK){
        if(*_debug){
            printf("Inst %d, _modbus_tcp_send(): tcp_output failed \"%s\"\n",
                   peer->instance, err_txt(rc));
        }
        _modbus_tcp_close(ctx);

        peer->error = rc;
        errno = ECONNRESET;
        return -1;
    }
    DEBUG_printf("Inst %d, _modbus_tcp_send(): tcp_output OK\n",
                 peer->instance);

    struct timeval *ptv;
    int timeout_ms = 0;
    ptv = &ctx->response_timeout;

    DEBUG_printf("Inst %d, _modbus_tcp_send(): timeout: ",
                 peer->instance);
    if(ptv){
        timeout_ms =  ptv->tv_sec * 1000;
        timeout_ms += ptv->tv_usec / 1000;
        DEBUG_printf("%d ms\n", timeout_ms);
    }
    else{
        DEBUG_printf("none\n");
    }

    while(peer->send_len == 0 && peer->error == ERR_OK){
        if(ptv){
            if(timeout_ms == 0){
                if(*_debug){
                    printf("Inst %d, _modbus_tcp_send(): Timeout!\n", ctx->s);
                }
                errno = ETIMEDOUT;
                return -1;
            }
            timeout_ms -= _WAIT_LOOP_INTERVAL_MS;
        }
        sleep_ms(_WAIT_LOOP_INTERVAL_MS);
    }
    if(*_debug && ptv){
        _show_timeout(ctx->s, ptv, timeout_ms, "send");
    }

    if(peer->error == ERR_OK){
        DEBUG_printf("Inst %d, _modbus_tcp_send(): %d Bytes\n",
                   ctx->s, peer->send_len);
        return peer->send_len;
    }
    else{
        if(*_debug){
            printf("Inst %d, _modbus_tcp_send() failed: \"%s\"\n",
                   peer->instance, err_txt(peer->error));
        }
        _modbus_tcp_close(ctx);

        errno = ECONNRESET;
        return -1;
    }
}

static int _modbus_tcp_receive(modbus_t *ctx, uint8_t *req)
{
    return _modbus_receive_msg(ctx, req, MSG_INDICATION);
}

// SERVER AND CLIENT -> aka "peer"
// Receive data from peer, mimics the recv() system-call on linux
// errno used: EBADF, ECONNRESET
static ssize_t _modbus_tcp_recv(modbus_t *ctx, uint8_t *rsp, int rsp_length)
{
    PICO_ASSERT("ctx != NULL", ctx != NULL);
    if(ctx->s < 0 && ctx->s >= MAX_PEERS){
        if(*_debug){
            printf("_modbus_tcp_recv(): void id: %d\n", ctx->s);
        }
        return -1;
    }
    PICO_ASSERT("ctx->s >= 0 && ctx->s < MAX_PEERS",
                ctx->s >= 0 && ctx->s < MAX_PEERS);

    PICO_ASSERT("Waiting for a confirmation.", peers[ctx->s] != NULL);
    if(peers[ctx->s]->pcb == NULL){
        if(*_debug){
            printf("Inst %d, _modbus_tcp_recv(): non-existant connection\n",
                   ctx->s);
        }
        errno = ECONNRESET;
        return(-1);
    }
    PICO_ASSERT("peers[ctx->s]->connected == TRUE",
                peers[ctx->s]->connected == TRUE);

    tcp_connection *peer = peers[ctx->s];
    DEBUG_printf("Inst %d, _modbus_tcp_recv()\n", peer->instance);

    while(peer->recv_len == 0 && peer->error == ERR_OK){
        sleep_ms(_WAIT_LOOP_INTERVAL_MS);
    }

    if(peer->error != ERR_OK){
        if(*_debug)
            printf("Inst %d, _modbus_tcp_recv(), Error %s\n",
               peer->instance, err_txt(peer->error));
        if(peer->error == ERR_RST){
            errno = ECONNRESET;
        }
        else {
            errno = EBADF;
        }
        return -1;
    }

    int cpCnt = MIN(peer->recv_len, rsp_length);

    memcpy(rsp, peer->buffer_recv, cpCnt);
    if(cpCnt < peer->recv_len){
        memmove(peer->buffer_recv, peer->buffer_recv + cpCnt, peer->recv_len - cpCnt);
    }
    peer->recv_len -= cpCnt;

    return cpCnt;
}

static int _modbus_tcp_check_integrity(__unused modbus_t *ctx, __unused uint8_t *msg, const int msg_length)
{
    return msg_length;
}

static int _modbus_tcp_pre_check_confirmation(modbus_t *ctx,
                                              const uint8_t *req,
                                              const uint8_t *rsp,
                                              __unused int rsp_length)
{
    unsigned int protocol_id;
    /* Check transaction ID */
    if (req[0] != rsp[0] || req[1] != rsp[1]) {
        if (*_debug) {
            printf("Inst %d,\
Invalid transaction ID received 0x%X (not 0x%X)\n",
                (rsp[0] << 8) + rsp[1],
                (req[0] << 8) + req[1],
                ctx->s);
        }
        errno = EMBBADDATA;
        return -1;
    }

    /* Check protocol ID */
    protocol_id = (rsp[2] << 8) + rsp[3];
    if (protocol_id != 0x0) {
        if (*_debug) {
            printf("Inst %d, \
Invalid protocol ID received 0x%X (not 0x0)\n",
                protocol_id, ctx->s);
        }
        errno = EMBBADDATA;
        return -1;
    }

    return 0;
}

// int _modbus_tcp_set_ipv4_options(...)
//  No equivalent in pico-tcp

// int _connect(...)
//  No equivalent in pico-tcp

// CLIENT
// Connect to a server, the actual connection is established
// by _tcp_client_connect()
// Called without further ado from "modbus_connect(modbus_t *ctx)"
static int _modbus_tcp_connect(modbus_t *ctx){
    int timeout_ms = 0;

    PICO_ASSERT("ctx != NULL", ctx != NULL);
    PICO_ASSERT("peers[ctx->s]->instance >= 0 && peers[ctx->s]->instance < MAX_PEERS",
                peers[ctx->s]->instance>= 0 &&
                peers[ctx->s]->instance < MAX_PEERS);
    PICO_ASSERT("peers[ctx->s] != NULL", peers[ctx->s] != NULL);

    tcp_connection *server = peers[ctx->s];

    struct timeval *ptv;
    ptv = &ctx->response_timeout;

    DEBUG_printf("Inst %d, _modbus_tcp_connect() with timeout: ",
                 ctx->s);
    if(ptv){
        timeout_ms =  ptv->tv_sec * 1000;
        timeout_ms += ptv->tv_usec / 1000;
        DEBUG_printf("%d ms\n", timeout_ms);
    }
    else{
        DEBUG_printf("none\n");
    }

    server->error = ERR_INPROGRESS;
    if(_tcp_client_connect(server->instance) < 0){
        if(*_debug){
            printf("Inst %d, _modbus_tcp_connect(): \
_tcp_client_connect() failed.\n", server->instance);
        }

 // Now:
        // server->error = ERR_IF or ERR_MEM;
        _modbus_tcp_close(ctx);
        return -1;
    }

    while(server->error != ERR_OK){
        if(server->error != ERR_CONN){
            DEBUG_printf("Inst %d, _modbus_tcp_connect(): Error \"%s\"\n",
                         server->instance, err_txt(server->error));
            if(!(ctx->error_recovery & MODBUS_ERROR_RECOVERY_LINK)){
                server->error = ERR_RST;
                _modbus_tcp_close(ctx);
            }
            // Now:
            // server->error = ERR_RST;
            errno = ECONNRESET;
            return -1;
        }

        if(ptv){
            if(timeout_ms <= 0){
                if(*_debug){
                     printf("Inst %d, _modbus_tcp_connect(): Timeout!\n", server->instance);
                }

                server->error = ERR_TIMEOUT;
                errno = ETIMEDOUT;
                _modbus_tcp_close(ctx);
                return -1;
            }
            timeout_ms -= (_WAIT_LOOP_INTERVAL_MS * 100);
        }
        sleep_ms(_WAIT_LOOP_INTERVAL_MS * 100);
        // busy_wait_ms(_WAIT_LOOP_INTERVAL_MS * 100);
    }

    if(*_debug && ptv){
        _show_timeout(server->instance, ptv, timeout_ms,
                      "_modbus_tcp_connect");
    }

    if(server->error == ERR_OK){
        if(*_debug){
            printf("Inst %d, _modbus_tcp_connect(): connected to %s:%d.\n",
                   server->instance, server->ip, server->port);
        }
        server->connected = true;
        sleep_ms(100);
        _modbus_tcp_flush(ctx);
        return(0);
    }
    else{ //TODO macht das Sinn? Kommt jemals ECONNREFUSED???
        errno = ECONNREFUSED;
        if(*_debug){
            printf("Inst %d, _modbus_tcp_connect(): ERROR %s\n",
                   server->instance, strerror(errno));
        }
        _modbus_tcp_close(ctx);
        return -1;
    }
}

unsigned int _modbus_tcp_is_connected(modbus_t *ctx)
{
    PICO_ASSERT("ctx != NULL", ctx != NULL);

    DEBUG_printf("Inst: %d _modbus_tcp_is_connected()\n", ctx->s);

    if(ctx->s < 0 && ctx->s >= MAX_PEERS){
        if(*_debug){
            printf("Inst: %d  _modbus_tcp_is_connected(): void connection\n",
                   ctx->s);
        }
        return FALSE;
    }

    if(peers[ctx->s] == NULL)
        return FALSE;

    if(peers[ctx->s]->pcb  == NULL)
        return FALSE;

    if(peers[ctx->s]->error  == ERR_CLSD)
        return FALSE;

    return peers[ctx->s]->connected;
}

// SERVER AND CLIENT -> aka "peer"
/* Closes the network connection and socket in TCP mode */
void _modbus_tcp_close(modbus_t *ctx)
{
    PICO_ASSERT("ctx != NULL", ctx != NULL);
    if(ctx->s < 0 && ctx->s >= MAX_PEERS){
        if(*_debug){
            printf("_modbus_tcp_close(): void connection: %d\n", ctx->s);
        }
        return;
    }

    if(peers[ctx->s] == NULL){
        if(*_debug){
            printf("Inst %d,_modbus_tcp_close(): non-existant connection\n",
                   ctx->s);
        }
        return;
    }

    _cleanup_connection(ctx->s, "_modbus_tcp_close");
    return;
}

// SERVER AND CLIENT -> aka "peer"
static int _modbus_tcp_flush(modbus_t *ctx)
{
    PICO_ASSERT("ctx != NULL", ctx != NULL);
    if(ctx->s < 0 && ctx->s >= MAX_PEERS){
        if(*_debug){
            printf("_modbus_tcp_flush(): void connection: %d\n", ctx->s);
        }
        return 0;
    }
    if(peers[ctx->s]->pcb == NULL){
        if(*_debug){
            printf("Inst %d, _modbus_tcp_flush(): closed connection\n",
                   ctx->s);
        }
        return 0;
    }

    DEBUG_printf("Inst %d, _modbus_tcp_flush()\n", ctx->s);

    int bytes_to_flush = peers[ctx->s]->recv_len;
    peers[ctx->s]->recv_len = 0;

    return bytes_to_flush;
}


// int modbus_tcp_listen(...)
//  No equivalent in pico-tcp


// int modbus_tcp_accept(...)
//  No equivalent in pico-tcp


/* libmodbus/src/modbus.c
 * int _modbus_receive_msg(modbus_t *ctx, uint8_t *msg, msg_type_t msg_type)
 *	rc = ctx->backend->select(ctx, &rset, p_tv, length_to_read);
 *	if (rc == -1) {
 *		if (errno == ETIMEDOUT) {
 *		else if (errno == EBADF) {
 *
 * 1.) fd_set *rset is set to ctx->s
 *	// Add a file descriptor to the set
 *	FD_ZERO(&rset);
 *	FD_SET(ctx->s, &rset);
 *
 *	Therefore we can ignore rset and use ctx->s
 *
 * 2.) errno is not evaluated upstream of _modbus_receive_msg()
 */

// SERVER AND CLIENT -> aka "peer"
// Wait for data from peer, mimics (somehow) the select() system-call on linux
// If tv == NULL, wait forever, else return ERR_TIMEOUT if no data wihin given time.
// errno used: EBADF, ETIMEDOUT, ECONNRESET

static int
_modbus_tcp_select(modbus_t *ctx,
                   __unused fd_set *rset,
                   struct timeval *ptv,
                   __unused int length_to_read)
{
    PICO_ASSERT("ctx != NULL", ctx != NULL);

    if(ctx->s < 0 && ctx->s >= MAX_PEERS){
        if(*_debug){
            printf("_modbus_tcp_select(): void connection: %d\n", ctx->s);
        }
        return -1;
    }
    if(peers[ctx->s]->pcb == NULL){
        if(*_debug)
            printf("Inst %d, _modbus_tcp_select(): non-existant connection\n",
                   ctx->s);
        errno = ECONNRESET;
        return(-1);
    }

    DEBUG_printf("Inst %d, _modbus_tcp_select()\n", ctx->s);
    tcp_connection *peer = peers[ctx->s];

    int peerId = -1;
    int timeout_ms = 0;

    peerId = ctx->s;

    PICO_ASSERT("peerId == ctx->s", peerId == ctx->s);

    if(!peer->connected){
        // connectionclosed during a transmission
        if(*_debug){
            printf("Inst %d, _modbus_tcp_select(): closed connection\n",
                   ctx->s);
        }
        errno = ECONNRESET;
        return(-1);
    }

    if(ptv){
        timeout_ms =  ptv->tv_sec * 1000;
        timeout_ms += ptv->tv_usec / 1000;
    }
    else{
        timeout_ms = 0;
    }

    while(peer->error == ERR_OK && peer->recv_len == 0){
        if(ptv){
           if(timeout_ms <= 0){
                DEBUG_printf("Inst %d, _modbus_tcp_select(): \
Timeout! Error: %s\n", ctx->s, err_txt(peer->error));
                errno = ETIMEDOUT;
                return -1;
            }
            sleep_ms(_WAIT_LOOP_INTERVAL_MS);
            timeout_ms -= _WAIT_LOOP_INTERVAL_MS;
        }
    }
    if(*_debug && ptv){
        _show_timeout(peer->instance, ptv, timeout_ms,
                      "_modbus_tcp_select");
    }

    if(peer->error != ERR_OK){
        if(*_debug){
            printf("Inst %d, _modbus_tcp_select(), Error %s\n",
               peerId, err_txt(peer->error));
        }
        if(peer->error == ERR_RST || peer->error == ERR_ABRT){
            // peer closed connection or idle-timeout
            errno = ECONNRESET;
        }
        else {
            errno = EBADF;
        }
        _modbus_tcp_close(ctx);
        return -1;
    }
    return peerId;
}

static void _modbus_tcp_free(modbus_t *ctx)
{
    for (int i = 0; i < MAX_PEERS; i++) {
        if(peers[i] != NULL){
            _cleanup_connection(i, "_modbus_tcp_free");
            if(*_error_recovery & MODBUS_ERROR_RECOVERY_LINK){
                _free_connection(peers[i]);
            }
        }
    }
    free(ctx);
}

// clang-format off
const modbus_backend_t _modbus_tcp_backend = {
    _MODBUS_BACKEND_TYPE_TCP,
    _MODBUS_TCP_HEADER_LENGTH,
    _MODBUS_TCP_CHECKSUM_LENGTH,
    MODBUS_TCP_MAX_ADU_LENGTH,
    _modbus_set_slave,
    _modbus_tcp_build_request_basis,
    _modbus_tcp_build_response_basis,
    _modbus_tcp_get_response_tid,
    _modbus_tcp_send_msg_pre,
    _modbus_tcp_send,
    _modbus_tcp_receive,
    _modbus_tcp_recv,
    _modbus_tcp_check_integrity,
    _modbus_tcp_pre_check_confirmation,
    _modbus_tcp_connect,
    _modbus_tcp_is_connected,
    _modbus_tcp_close,
    _modbus_tcp_flush,
    _modbus_tcp_select,
    _modbus_tcp_free,
    modbus_tcp_mapping_lock,
    modbus_tcp_mapping_unlock
};

// clang-format on

// modbus_t *modbus_new_tcp(const char *ip, int port)
//  No equivalent direct in pico-tcp
//  Replaced by tcp_server_init() and tcp_client_init()


/******************************************
 * PICO specific functons
 ******************************************/

/*************************************************************
 * The LWIP callback functions
 *************************************************************/
// SERVER:
// Callback for incoming connections ("accepted")
// Handle the incoming TCP connection from a client
static err_t accept_callback(__unused void *arg, struct tcp_pcb *newpcb, err_t err) {
    if (err != ERR_OK) {
        if(*_debug){
            printf("accept_callback(): Error accepting client: %d\n", err);
        }
        return ERR_MEM;  // Reject the connection
    }

    DEBUG_printf("accept_callback(): Client connected from %s:%d\n",
               ipaddr_ntoa(&(newpcb->remote_ip)),
               newpcb->remote_port);

    for (int i = 0; i < MAX_PEERS; i++) {
        // This should nevet be neccesary...
        if (peers[i] != NULL && peers[i]->error != ERR_CLSD &&
                (peers[i]->last_activity + (CLIENT_TIMEOUT * 1000)) <
                (time_us_64() / 1000)) {

            if(*_debug){
                printf("accept_callback(): found idle client %d!\n", i);
            }
            if(_cleanup_connection(i, "accept_callback")){
                if(*_debug){
                    printf("accept_callback(), _cleanup_connection(): OK\n");
                }
            }
        }

        if(peers[i] == NULL){
            DEBUG_printf("accept_callback(): New client, slot: %d\n", i);

            tcp_connection *client = malloc(sizeof(tcp_connection));
            if(client == NULL){
                if(*_debug)
                    printf("accept_callback(): out of memory\n");
                return ERR_MEM;
            }

            memset((void *)client, 0, sizeof(tcp_connection));
            peers[i] = client;

            client->instance 		= i;
            client->connected       = true;
            client->error 			= ERR_OK;
            client->pcb 			= newpcb;
            client->last_activity 	= time_us_64() / 1000;
            strcpy(client->ip, ipaddr_ntoa(&(newpcb->remote_ip)));

            cyw43_arch_lwip_begin();
            tcp_recv(newpcb, receive_callback);
            tcp_err(newpcb,  error_callback);
            tcp_sent(newpcb, sent_callback);
            if(CLIENT_TIMEOUT > 0){
                tcp_poll(newpcb, poll_callback, 10);
            }
            tcp_arg(newpcb, (void *) client);
            cyw43_arch_lwip_end();

            DEBUG_printf("accept_callback(): New client, slot: %d READY\n", i);
            return ERR_OK;
        }
    }

    // No available slot for a new client, reject the connection
    if(*_debug)
        printf("accept_callback(): Max peers, rejecting connection.\n");

    cyw43_arch_lwip_begin();
    err_t rc =  tcp_close(newpcb);
    cyw43_arch_lwip_end();
    if (rc != ERR_OK) {
        if(*_debug){
            printf("accept_callback(): tcp_close() failed\n");
        }
        cyw43_arch_lwip_begin();
        tcp_abort(newpcb);
        cyw43_arch_lwip_end();
        return ERR_ABRT;
    }

    if(*_debug){
        printf("accept_callback(): Rejected new client\n");
    }
    return ERR_OK;
}

// CLIENT:
// Callback for outgoing connections ("connected")
// Handle the outgoing TCP connection to a server
err_t connect_callback(void *arg, struct tcp_pcb *tpcb, err_t err){
    tcp_connection *server = (tcp_connection *)arg;

    PICO_ASSERT("server != NULL",
                server != NULL);

    DEBUG_printf("Inst %d, connect_callback()\n",server->instance);

    PICO_ASSERT("server->pcb != NULL",
                server->pcb != NULL);

    if( server->pcb != tpcb){
        if(*_debug){
            printf("server->pcb != tpcb\n");
        }
        server->error= ERR_CONN;
        return ERR_CONN;
    }

    if (err != ERR_OK) {
        if(*_debug){
            printf("Inst %d, Error connectimg to server. Error: %s\n",
                server->instance, err_txt(server->error));
        }

        server->error= err;
        // TODO: is this return OK?
        return err;
    }

    server->error = ERR_OK;
    DEBUG_printf("Inst %d, connect_callback() OK!\n", server->instance);

    return ERR_OK;
}

// SERVER AND CLIENT -> aka "peer"
// Callback when data is received from peer
static err_t receive_callback(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {

    if(arg == NULL){
        if(*_debug){
            printf("receive_callback(), non-existant or closed connection\n");
        }
        if (p != NULL)
            pbuf_free(p);
        return ERR_OK;
    }

    tcp_connection *peer = (tcp_connection *)arg;

    if(err != ERR_OK){
        if(*_debug){
            printf("Inst %d, receive_callback(), error: %s\n",
                   peer->instance, err_txt(err));
        }
        if (p != NULL)
            pbuf_free(p);
        return ERR_OK;
    }

    if(peer->connected == false){
        if(*_debug){
            printf("Inst %d, receive_callback(), connection  already closed\n",
                   peer->instance);
        }
        return ERR_OK;
    }


    PICO_ASSERT("peer->pcb != NULL",
                peer->pcb != NULL);
    PICO_ASSERT("peer->pcb == pcb",
                peer->pcb == pcb);

    DEBUG_printf("Inst %d, receive_callback()\n", peer->instance);

    if (p != NULL) {
         // Receive the buffer
        if (p->tot_len > 0) {
            DEBUG_printf("Inst %d, receive_callback(), initial recv_len: %d\n",
                         peer->instance, peer->recv_len);

            const uint16_t buffer_left =
                        MODBUS_TCP_MAX_ADU_LENGTH - peer->recv_len;
            if(p->tot_len <= buffer_left){
                cyw43_arch_lwip_begin();
                peer->recv_len +=
                pbuf_copy_partial(
                    p,
                    peer->buffer_recv + peer->recv_len,
                    p->tot_len,
                    0);

                tcp_recved(pcb, p->tot_len);
                cyw43_arch_lwip_end();
                peer->error = ERR_OK;
                // tcp_error[peer->instance] = ERR_OK;
                DEBUG_printf("Inst %d, recv_len is now: %d\n",
                             peer->instance, peer->recv_len);
            }
            else{
                peer->recv_len = 0;
                peer->error = ERR_VAL;
                if(*_debug){
                    printf("Inst %d, receive_callback(), message too long\n",
                           peer->instance);
                }
            }
            cyw43_arch_lwip_begin();
            pbuf_free(p);
            cyw43_arch_lwip_end();
        }
        // Update the last activity time for this peer
        peer->last_activity = time_us_64() / 1000;

        return ERR_OK;
    }
    else{ // p == NULL
        if(*_debug){
            printf("Inst %d, receive_callback(), peer closed connection. %s\n",
                   peer->instance, err_txt(err));
        }
        _cleanup_connection(peer->instance, "receive_callback");
        return ERR_CLSD;
    }
}

// SERVER AND CLIENT -> aka "peer"
// Callback when data has been sent successfully
static err_t sent_callback(void *arg, struct tcp_pcb *pcb, u16_t len) {
    if(arg == NULL){
        if(*_debug){
            printf("sent_callback() called for non-existant connection\n");
        }
        return ERR_OK;
    }
    tcp_connection *peer = (tcp_connection *)arg;

    PICO_ASSERT("peer != NULL",
                peer != NULL);
    PICO_ASSERT("peer->pcb != NULL",
                peer->pcb != NULL);
    PICO_ASSERT("peer->instance >= 0 && peer->instance < MAX_PEERS",
                peer->instance>= 0 && peer->instance < MAX_PEERS);
    PICO_ASSERT("peer->connected == TRUE",
                peer->connected == TRUE);
    PICO_ASSERT("peer->pcb == pcb",
                peer->pcb == pcb);

    DEBUG_printf("Inst %d, sent_callback(), peer acknowleged %d bytes\n",
                 peer->instance, len);

    // Update the last activity time for this peer
    peer->send_len = len;
    peer->error = ERR_OK;
    peer->last_activity = time_us_64() / 1000;

    return ERR_OK;
}

// SERVER
// Callback for polling
// Closes a connection that has been idle for too long
static err_t poll_callback(void *arg, __unused struct tcp_pcb *pcb) {
     tcp_connection *client = ( tcp_connection *)arg;
    if(client == NULL){
        return ERR_OK;
    }

    if (client->error != ERR_CLSD &&
            (client->last_activity + (CLIENT_TIMEOUT * 1000)) <
                (time_us_64() / 1000)) {

        if(*_debug){
            printf("poll_callback(%d): timeout!\n", client->instance);
        }

        // No need to call _free_connection if MODBUS_ERROR_RECOVERY_LINK
        // becouse this callback is for servers only and servers dont have
        // MODBUS_ERROR_RECOVERY_LINK set
        if(_cleanup_connection(client->instance, "poll_callback")){
            if(*_debug){
                printf("poll_callback(): OK\n");
            }
        }
    }
    return ERR_OK;
}

// SERVER AND CLIENT -> aka "peer"
// Callback for error handling
// Manual: The corresponding pcb is already freed when this callback is called!
static void error_callback(void *arg, err_t err) {
    // Clean up the peer on error

    if(arg != NULL){
        tcp_connection *peer = (tcp_connection *)arg;
        DEBUG_printf("Inst %d, error_callback(): TCP connection error %s\n",
                     peer->instance, err_txt(err));
        PICO_ASSERT("peer->instance >= 0 && peer->instance < MAX_PEERS",
                    peer->instance >= 0 && peer->instance < MAX_PEERS);

        peer->error = err;
    }
    else{
        if(*_debug){
            printf("error_callback() TCP connection error %s. \
TRIGGERED FOR CLOSED CONNECTION\n", err_txt(err));
        }
    }
}

/*************************************************************
 * END of LWIP callback functions
 *************************************************************/

// SERVER:
// Initialize the TCP server
modbus_t *tcp_server_init(int port) {
    modbus_t *ctx;
    struct tcp_pcb *pcb;
    err_t err;

    // Initialize the client list
    for (int i = 0; i < MAX_PEERS; i++) {
        peers[i] = NULL;
    }

    ctx = (modbus_t *) malloc(sizeof(modbus_t));
    if (ctx == NULL) {
        return NULL;
    }
    _modbus_init_common(ctx);

    ctx->slave = MODBUS_TCP_SLAVE;
    critical_section_init(&(ctx->cs));
    ctx->backend = &_modbus_tcp_backend;
    ctx->backend_data = NULL;

    _debug = &(ctx->debug);
    _error_recovery = &(ctx->error_recovery);

    // Create a new TCP control block
    cyw43_arch_lwip_begin();
    pcb = tcp_new();
    if (pcb == NULL) {
        if(*_debug){
            printf("tcp_server_init(): Error creating PCB.\n");
        }
        cyw43_arch_lwip_end();
        return NULL;
    }

    // Bind the server to the desired port
    err = tcp_bind(pcb, IP_ADDR_ANY, port);
    if (err != ERR_OK) {
        if(*_debug){
            printf("tcp_server_init(): Error binding server to port %d.\n", port);
        }
        cyw43_arch_lwip_end();
        return NULL;
    }

    // Set the listening callback function
    pcb = tcp_listen(pcb);
    tcp_accept(pcb, accept_callback);
    cyw43_arch_lwip_end();

    DEBUG_printf("Server listening on port %d...\n", port);
    return ctx;
}

// CLIENT
// Initialize modbus
modbus_t *tcp_client_init(void)
{
    modbus_t *ctx;

    // Initialize the server list
    for (int i = 0; i < MAX_PEERS; i++) {
        peers[i] = NULL;
    }

    ctx = (modbus_t *) malloc(sizeof(modbus_t));
    if (ctx == NULL) {
        return NULL;
    }
    _modbus_init_common(ctx);

    /* Could be changed after to reach a remote serial Modbus device */
    ctx->slave = MODBUS_TCP_SLAVE;
    critical_section_init(&(ctx->cs));
    ctx->backend = &_modbus_tcp_backend;
    ctx->backend_data = NULL;

    _debug = &(ctx->debug);
    _error_recovery = &(ctx->error_recovery);

    return ctx;
}

// CLIENT:
// Initialize the TCP client
int tcp_new_client(const char *server_ip, int port) {
    for (int i = 0; i < MAX_PEERS; i++) {
        if(peers[i] == NULL){

            tcp_connection *server = malloc(sizeof(tcp_connection));
            if(server == NULL){
                if(*_debug){
                    printf("tcp_new_client(): not enough memory.\n");
                }
                errno = ENOMEM;
                return -1;
            }

            memset((void *)server, 0, sizeof(tcp_connection));
            peers[i] = server;

            server->instance 		= i;
            server->connected		= false;
            strcpy(server->ip, server_ip);
            server->port 			= port;

            return i;
        }
    }

    // No available slot for a new client, reject the connection
    if(*_debug){
        printf("Maximum peers reached, no connection available.\n");
    }
    errno = ENOMEM;
    return -1;
}

int serialNum = 0;
// CLIENT:
// Connect to server
static int _tcp_client_connect(int serverID) {
    struct tcp_pcb *pcb;
    ip_addr_t server_addr;
    err_t err;

    tcp_connection *server = peers[serverID];
    PICO_ASSERT("server != NULL", server != NULL);

    // Create a new TCP control block
    DEBUG_printf("Inst %d, _tcp_client_connect(): new server connection.\n",
                 serverID);

    cyw43_arch_lwip_begin();
    pcb = tcp_new();
    cyw43_arch_lwip_end();
    if (pcb == NULL) {
        if(*_debug){
            printf("Inst %d, _tcp_client_connect(): Error creating PCB.\n",
                   serverID);
        }
        server->error = ERR_MEM;
        errno = ENOMEM;
        return -1;
    }

    server->pcb 			= pcb;
    server->error 			= ERR_CONN;

    DEBUG_printf("Inst %d, trying to connect to server %s:%d\n",
           serverID, server->ip, server->port);

    ip4addr_aton(server->ip, &server_addr);
    cyw43_arch_lwip_begin();
    tcp_recv(pcb, receive_callback);
    tcp_err(pcb,  error_callback);
    tcp_sent(pcb, sent_callback);
    tcp_arg(pcb, (void *) server);

    err = tcp_connect(
        server->pcb, &server_addr, server->port, connect_callback);
    cyw43_arch_lwip_end();

    if (err != ERR_OK) {
        if(*_debug){
            printf("Inst %d, error connecting to server %s:%d. %s\n",
                     serverID, server->ip, server->port, err_txt(ERR_IF));
        }
        server->error = ERR_IF;
        errno = ENOTCONN;
        return -1;
    }

    return 0;
}

// SERVER AND CLIENT -> aka "peer"
// Send data to peer, mimics the send() system-call on linux
// errno used: EBADF, ECONNRESET
// errno from linux send() UNUSED: EPIPE
// Cant use "static" here, becouse ist called from pico-unit-test-server.c
int _send(int ctx_s, void *buf, size_t len) {
    tcp_connection *peer;

    err_t rc;

    PICO_ASSERT("ctx_s >= 0 && ctx_s < MAX_PEERS",
                ctx_s >= 0 && ctx_s < MAX_PEERS);
    PICO_ASSERT("peers[ctx_s] != NULL",
                peers[ctx_s] != NULL);

    peer = peers[ctx_s];
    DEBUG_printf("Inst %d, _send()\n", peer->instance);

    if(peer->error == ERR_CLSD){
        DEBUG_printf("Inst %d, _send(), connection is closing\n",
                     peer->instance);
        errno = ECONNRESET;
        return -1;
    }

    peer->send_len = 0;
    cyw43_arch_lwip_begin();
    rc = tcp_write(peer->pcb, buf, len, TCP_WRITE_FLAG_COPY);
    cyw43_arch_lwip_end();
    if(rc != ERR_OK){
        peer->error = rc;
        errno = ECONNRESET;
        return -1;
    }

    cyw43_arch_lwip_begin();
    rc = tcp_output(peer->pcb);
    cyw43_arch_lwip_end();
    if(rc != ERR_OK){
        peer->error = rc;
        errno = ECONNRESET;
        return -1;
    }

    while(peer->send_len == 0 && peer->error == ERR_OK){
        sleep_ms(_WAIT_LOOP_INTERVAL_MS);
    }
    if(peer->error == ERR_OK){
        return peer->send_len;
    }
    else{
        errno = ECONNRESET;
        return -1;
    }
}

// CLIENT
int modbus_is_connected(int peer_id){
    if(peer_id >= 0 && peer_id < MAX_PEERS &&
            peers[peer_id] != NULL &&
            peers[peer_id]->pcb != NULL &&
            peers[peer_id]->connected &&
            peers[peer_id]->error == ERR_OK){
        return 1;
    }
    else{
        return 0;
    }
}

// SERVER and CLIENT
char* modbus_get_ip(int peer_id){
    if(peer_id >= 0 && peer_id < MAX_PEERS &&
        peers[peer_id] != NULL ){
    return peers[peer_id]->ip;
    }
    else{
        return NULL;
    }
}
// SERVER and CLIENT
int modbus_get_id(modbus_t *ctx){
    PICO_ASSERT("ctx != NULL", ctx != NULL);

    return ctx->s;
}

// SERVER
/* Checks whether there is an existing connection under client_id.
 * If so, checks whether the client has sent a request or
 * whether an error has occurred (e.g., the client has disconnected).
 *
 * Return values:
 * >0 Nuber of bytes received so far
 * 0 No request
 * <0 An error has occurred. The returned value is a LWIP-error
 */
int modbus_client_status(int client_id) {
    PICO_ASSERT("client_id >= 0 && client_id < MAX_PEERS",
                client_id >= 0 && client_id < MAX_PEERS);

    if(peers[client_id] != NULL && peers[client_id]->connected){
        tcp_connection *client = peers[client_id];

        if(client->error != ERR_OK){
            return client->error;
        }

        return client->recv_len;
    }
    return(0);
}

// Check whether the request has modified any data in one of
// the Modbus tables. In other words, check if it was a write
// (or read/write)operation rather than a read-only one.
bool _modbus_tcp_message(modbus_t *ctx, const uint8_t *req, modbus_message_t *msg)
{
    unsigned int offset;

    offset = ctx->backend->header_length;
    msg->func  = req[offset];

    switch (msg->func) {
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

// If the request has modified any data in one of
// the Modbus tables, push a message for core 0 into the FIFO.
void modbus_notify_if_write(modbus_t *ctx,
                             const uint8_t *req,
                             modbus_message_t *msg)
{
    if(_modbus_tcp_message(ctx, req, msg)){
        multicore_fifo_push_blocking((int32_t)msg);
    }
}

// Check wether a client has modified any data in one of the
// Modbus tables
modbus_message_t *modbus_write_notify(void)
{
    if(multicore_fifo_rvalid()){
        return ( modbus_message_t *) multicore_fifo_pop_blocking();
    }
    else{
        return NULL;
    }
}

void modbus_tcp_mapping_lock(modbus_t *ctx)
{
    critical_section_enter_blocking(&(ctx->cs));
}

void modbus_tcp_mapping_unlock(modbus_t *ctx)
{
    critical_section_exit(&(ctx->cs));
}

int modbus_tcp_get_error(void)
{
    return errno;
}

bool modbus_get_debug(modbus_t *ctx)
{
    return ctx->debug;
}

// This function is only used in test cases (pico-unit-test-server)
void modbus_tcp_nodelay(modbus_t *ctx, bool enable)
{
    struct tcp_pcb *pcb = peers[ctx->s]->pcb;

    if(enable)
        tcp_nagle_disable(pcb);
    else
        tcp_nagle_enable(pcb);
}

/* Theese are lwip error-codes, not linux errors.
 * For errno see: /usr/include/newlib/sys/errno.h
 */
char *err_str[] = {
    /** No error, everything OK. */
    "ERR_OK",
    /** Out of memory error.     */
    "ERR_MEM",
    /** Buffer error.            */
    "ERR_BUF",
    /** Timeout.                 */
    "ERR_TIMEOUT",
    /** Routing problem.         */
    "ERR_RTE",
    /** Operation in progress    */
    "ERR_INPROGRESS",
    /** Illegal value.           */
    "ERR_VAL",
    /** Operation would block.   */
    "ERR_WOULDBLOCK",
    /** Address in use.          */
    "ERR_USE",
    /** Already connecting.      */
    "ERR_ALREADY",
    /** Conn already established.*/
    "ERR_ISCONN",
    /** Not connected.           */
    "ERR_CONN",
    /** Low-level netif error    */
    "ERR_IF",
    /** Connection aborted.      */
    "ERR_ABRT",
    /** Connection reset.        */
    "ERR_RST",
    /** Connection closed.       */
    "ERR_CLSD",
    /** Illegal argument.        */
    "ERR_ARG"
};

char *err_txt(int err){
    return err_str[-err];
}

bool _cleanup_connection(int peerID, char *context){

    DEBUG_printf("Inst %d: _cleanup_connection(from %s)\n", peerID, context);

    PICO_ASSERT("peerID >= 0 && peerID < MAX_PEERS",
                    peerID >= 0 && peerID < MAX_PEERS);

    tcp_connection *peer = peers[peerID];
    if(peer == NULL){
        DEBUG_printf("Inst %d: _cleanup_connection(from %s): \
already deleted.\n", peerID, context);
        return true;
    }
    if(peer->pcb == NULL){
        DEBUG_printf("Inst %d: _cleanup_connection(from %s): \
pcb already deleted.\n", peerID, context);
    }
    DEBUG_printf("Inst %d: _cleanup_connection(from %s): \
error \"%s\".\n", peerID, context, err_txt(peer->error));

    if(peer->pcb != NULL && peer->error != ERR_CLSD && peer->error != ERR_RST){
        DEBUG_printf("Inst %d: _cleanup_connection(from %s): \
deleting pcb.\n", peerID, context);
        cyw43_arch_lwip_begin();
        //TODO Clean-up after verfication
        tcp_arg(peer->pcb, (void *) NULL);
        tcp_recv(peer->pcb, NULL);
        tcp_sent(peer->pcb, NULL);
        tcp_err(peer->pcb, NULL);

        err_t err =  tcp_close(peer->pcb);
        cyw43_arch_lwip_end();

        if (err != ERR_OK) {
            if(*_debug){
                printf("Inst %d (from %s): _cleanup_connection(): \
tcp_close() failed\n", peerID, context);
            }
            return false;
        }
    }
    else{
        DEBUG_printf("Inst %d: _cleanup_connection(from %s): \
pcb NOT deleted.\n", peerID, context);
    }

    peer->pcb = NULL;
    peer->error = ERR_CLSD;
    peer->connected = FALSE;

    if(!(*_error_recovery & MODBUS_ERROR_RECOVERY_LINK)){
        DEBUG_printf("Inst %d: _cleanup_connection(from %s): \
calling _free_connection().\n", peerID, context);
         _free_connection(peer);
    }
    else{
        DEBUG_printf("Inst %d: _cleanup_connection(from %s): \
NOT calling _free_connection().\n", peerID, context);
    }
    return true;
}

// SERVER AND CLIENT -> aka "peer"
err_t _free_connection(tcp_connection *peer) {
    PICO_ASSERT("peer != NULL", peer != NULL);

    DEBUG_printf("Inst %d, free connection, last error was: %s\n",
                 peer->instance, err_txt(peer->error));

    int client_id = peer->instance;
    free(peers[client_id]);
    peers[client_id] = NULL;

    return ERR_OK;
}

void _show_timeout(int id, struct timeval *ptv, int remaining_ms, char *context){
    int timeout_ms = 0;

    if(ptv == NULL){
        return;
    }

    timeout_ms =  ptv->tv_sec * 1000;
    timeout_ms += ptv->tv_usec / 1000;

    printf("Inst %d, _show_timeout(), (from %s) \
timeout: %d, remaining: %d elapsed:%d\n",
           id, context, timeout_ms, remaining_ms, timeout_ms - remaining_ms);
}

void _show_peers(void){

    for(int i = 0; i < MAX_PEERS; i++){
        if(peers[i]){
            int64_t ttl; // time (in ms) until timeout occures
            ttl = peers[i]->last_activity + (CLIENT_TIMEOUT * 1000);
            ttl -= (time_us_64() / 1000);
            printf("%d: %s (TTL: %lld[s]) \n", i, peers[i]->ip, ttl / 1000);
        }
        else{
            printf("%d: available\n", i);
        }
    }
}
