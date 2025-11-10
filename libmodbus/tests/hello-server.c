/*
 * Copyright © Gerhard Schiller 2025, <gerhard.schiller@pm.me>
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This programm is the server to test pico-hello-client from the
 * examples directory.
 *
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdlib.h>
#include <string.h>

#include <modbus.h>

// The layout of the Modbus tabels for this server:

// None in the "bits" table (Coils)
#define NB_BITS				1
// None in the "input_bits" table (Discrete inputs)
#define NB_INPUT_BITS		0
// 1 word (2 bytes) in "Registers" table (Holding Registers)
// This is where the number sent by the client is stored
#define NB_REGISTERS		1
// 1 word (2 bytes) in the "input_registers" table (Input Registers)
// for square of the number sent by the client
#define NB_INPUT_REGISTERS	1

int main(void)
{
    int s = -1;
    modbus_t *ctx;
    modbus_mapping_t *mb_mapping;
    uint8_t query[MODBUS_TCP_MAX_ADU_LENGTH];
    int rc;

    // this directs modbus_tcp_listen() to listen on INADDR_ANY
    ctx = modbus_new_tcp("0.0.0.0", 502);

    // Allocate memory for the 4 Modbus tabels.
    mb_mapping =
        modbus_mapping_new(NB_BITS, NB_INPUT_BITS,
                       NB_REGISTERS, NB_INPUT_REGISTERS);
    if (mb_mapping == NULL) {
        fprintf(stderr, "Failed to allocate the mapping: %s\n", modbus_strerror(errno));
        modbus_free(ctx);
        return -1;
    }

    s = modbus_tcp_listen(ctx, 1);
    modbus_tcp_accept(ctx, &s);


    for (;;) {
        rc = modbus_receive(ctx, query);
        if (rc > 0) {
            modbus_reply(ctx, query, rc, mb_mapping);
        }
        else if (rc == -1) {
            printf("Connection to server lost\n");
            return 1;
        }

        int offset = modbus_get_header_length(ctx);
        int func  = query[offset];

        if(func == MODBUS_FC_WRITE_SINGLE_REGISTER){
            if((query[offset + 1] << 8) + query[offset + 2] == 0){
                int value = mb_mapping->tab_registers[0];
                mb_mapping->tab_input_registers[0] = value * value;
                printf("Got: %d, provided %d\n",
                       mb_mapping->tab_registers[0],
                       mb_mapping->tab_input_registers[0]);
           }
        }
    }
}
