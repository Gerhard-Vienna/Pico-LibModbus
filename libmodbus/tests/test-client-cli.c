/*
 * Copyright © Gerhard Schiller 2024, <gerhard.schiller@pm.me>
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This programm allows you to send requests to a modbus server.
 * The goal of this program is to check all major functions of
 *   libmodbus:
 *   - write_coil
 *   - read_bits
 *   - write_coils
 *   - write_register
 *   - read_registers
 *   - write_registers
 *   - read_registers
 *
 *   All these functions are called with user defined values and addresses
 *
 * This file is not a part of the libmodbus library by Stéphane Raimbault.
 */
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <modbus.h>

#define MAX_QUANTITY  16
#define BUFFSIZE      128

void showCodes(void);


void showCodes(void)
{
    printf(" 1 Read Coils                       2 Read Discrete Inputs\n");
    printf(" 3 Read Holding Registers           4 Read Input Registers\n");
    printf("\n");
    printf(" 5 Write Single Coil               15 Write Multiple Coils\n");
    printf(" 6 Write Single Register           16 Write Multiple Registers\n");
    printf("23 Read/Write Multiple Registers   22 Mask Write Register\n");
    printf("\n");
    printf("<CTR>+<D> to quit.\n");
}

int main(int argc, char *argv[])
{
    modbus_t *ctx;
    char *ip_or_device;
    int port;

    char input[BUFFSIZE];
    int code;
    int addr = 0, addr2 = 0;
    int nb = 0, nb2 = 0;

    char delimiter[] = ",; \n";
    char *ptr;
    int i;

    uint8_t  tab_wr_bits[MAX_QUANTITY];
    uint8_t  tab_rd_bits[MAX_QUANTITY];
    uint16_t tab_wr_registers[MAX_QUANTITY];
    uint16_t tab_rd_registers[MAX_QUANTITY];

    int rc;

    if (argc != 3) {
        printf("Modbus Client\n");
        printf("Usage:\n  %s IP Port\n", argv[0]);
        printf("\tEg. 10.0.0.1 502\n\n");
        exit(1);
    }

    ip_or_device = argv[1];
    port = atoi(argv[2]);

    printf("Test client at %s:%d\n", ip_or_device, port);

    ctx = modbus_new_tcp(ip_or_device, port);
    if (ctx == NULL) {
        fprintf(stderr, "Unable to allocate libmodbus context\n");
        return -1;
    }

    modbus_set_debug(ctx, FALSE);
    modbus_set_response_timeout(ctx, 3, 0);

    if (modbus_connect(ctx) == -1) {
        fprintf(stderr, "Connection failed: %s\n", modbus_strerror(errno));
        modbus_free(ctx);
        return -1;
    }

    memset(tab_wr_bits, 0, sizeof(tab_wr_bits));
    memset(tab_rd_bits, 0, sizeof(tab_rd_bits));
    memset(tab_wr_registers, 0, sizeof(tab_wr_registers));
    memset(tab_rd_registers, 0, sizeof(tab_rd_registers));

    showCodes();
    for(;;){
        printf("\nModbus Code (? for help): ");
        if(fgets(input, BUFFSIZE , stdin) == NULL){
            printf("\nQuit\n");
            exit(0);
        }
        if(input[0] == '?'){
            showCodes();
            continue;
        }

        code = atoi(input);
        switch(code){
            case MODBUS_FC_WRITE_SINGLE_COIL:
            case MODBUS_FC_WRITE_SINGLE_REGISTER:
            case MODBUS_FC_WRITE_MULTIPLE_COILS:
            case MODBUS_FC_WRITE_MULTIPLE_REGISTERS:
                printf("Address: ");
                if(fgets(input, BUFFSIZE , stdin) == NULL){
                    printf("\nQuit\n");
                    exit(0);
                }
                addr = atoi(input);
                nb = 1;
                break;

            case MODBUS_FC_READ_DISCRETE_INPUTS:
            case MODBUS_FC_READ_COILS:
            case MODBUS_FC_READ_HOLDING_REGISTERS:
            case MODBUS_FC_READ_INPUT_REGISTERS:
                printf("Start Address: ");
                if(fgets(input, BUFFSIZE , stdin) == NULL){
                    printf("\nQuit\n");
                    exit(0);
                }
                addr = atoi(input);
                printf("Quantity: ");
                if(fgets(input, BUFFSIZE , stdin) == NULL){
                    printf("\nQuit\n");
                    exit(0);
                }
                nb = atoi(input);
                break;

            case MODBUS_FC_WRITE_AND_READ_REGISTERS:
                printf("Start Read Address: ");
                if(fgets(input, BUFFSIZE , stdin) == NULL){
                    printf("\nQuit\n");
                    exit(0);
                }
                addr = atoi(input);

                printf("Read Quantity: ");
                if(fgets(input, BUFFSIZE , stdin) == NULL){
                    printf("\nQuit\n");
                    exit(0);
                }
                nb = atoi(input);

                printf("Start Write Address: ");
                if(fgets(input, BUFFSIZE , stdin) == NULL){
                    printf("\nQuit\n");
                    exit(0);
                }
                addr2 = atoi(input);

                printf("Write Quantity: ");
                if(fgets(input, BUFFSIZE , stdin) == NULL){
                    printf("\nQuit\n");
                    exit(0);
                }
                nb2 = atoi(input);
                break;

            case MODBUS_FC_READ_EXCEPTION_STATUS:
                printf("Not implemented yet\n\n");
                continue;
                break;

            case MODBUS_FC_MASK_WRITE_REGISTER:
                printf("Not implemented yet\n\n");
                continue;
                break;

            default:
                printf("Unknown MB-Code: %d\n\n", code);
                continue;
        }

        switch(code){
            case MODBUS_FC_WRITE_SINGLE_COIL:
            case MODBUS_FC_WRITE_MULTIPLE_COILS:
            case MODBUS_FC_WRITE_SINGLE_REGISTER:
            case MODBUS_FC_WRITE_MULTIPLE_REGISTERS:
            case MODBUS_FC_WRITE_AND_READ_REGISTERS:
                printf("Data to send: ");
                if(fgets(input, BUFFSIZE , stdin) == NULL){
                    printf("\nQuit\n");
                    exit(0);
                }
                ptr = strtok(input, delimiter);

                nb = 0;
                while(ptr != NULL && nb < MAX_QUANTITY) {
                    if(code == MODBUS_FC_WRITE_SINGLE_COIL
                        || code == MODBUS_FC_WRITE_MULTIPLE_COILS)
                        tab_wr_bits[nb++] = atoi(ptr);
                    else
                        tab_wr_registers[nb++] = atoi(ptr);
                    ptr = strtok(NULL, delimiter);
                }
//                 for(i = 0; i < MAX_QUANTITY; i++){
//                     if(code == MODBUS_FC_WRITE_SINGLE_COIL
//                         || code == MODBUS_FC_WRITE_MULTIPLE_COILS)
//                         printf("%d ",tab_wr_bits[i]);
//                     else
//                         printf("%u ",tab_wr_registers[i]);
//                 }
//                 printf("\n");
                break;
        }

        rc = -1;
        switch(code){
            case MODBUS_FC_WRITE_SINGLE_COIL:
               rc = modbus_write_bit(ctx, addr, tab_wr_bits[0]);
               printf("modbus_write_bit() at %d: %d\n", addr, tab_wr_bits[0]);
               break;

            case MODBUS_FC_WRITE_MULTIPLE_COILS:
                rc = modbus_write_bits(ctx, addr, nb, tab_wr_bits);
                printf("modbus_write_bits() at %d: %d bits\n", addr, nb);
                break;

            case MODBUS_FC_READ_COILS:
                rc = modbus_read_bits(ctx, addr, nb, tab_rd_bits);
                printf("modbus_read_bits() at %d: ", addr);
                for(i = 0; i < nb; i++)
                    printf("%d ", tab_rd_bits[i]);
                printf("\n");
                break;

            case MODBUS_FC_WRITE_SINGLE_REGISTER:
                rc = modbus_write_register(ctx, addr, tab_wr_registers[0]);
                printf("modbus_write_register() at %d: %d\n", addr, tab_wr_registers[0]);
                break;

            case MODBUS_FC_WRITE_MULTIPLE_REGISTERS:
                rc = modbus_write_registers(ctx, addr, nb, tab_wr_registers);
                printf("modbus_write_registers() at %d: %d register\n", addr, nb);
                break;

            case MODBUS_FC_READ_HOLDING_REGISTERS:
                rc = modbus_read_registers(ctx, addr, nb, tab_rd_registers);
                printf("modbus_read_registers() at %d: ", addr);
                for(i = 0; i < nb; i++)
                    printf("%d ", tab_rd_registers[i]);
                printf("\n");
                break;

            case MODBUS_FC_WRITE_AND_READ_REGISTERS:
                printf("modbus_write_and_read_registers()\n");
                printf("\tWrite at %d: ", addr2);
                rc = modbus_write_and_read_registers(
                    ctx,
                    addr2, nb2, tab_wr_registers,
                    addr,  nb,  tab_rd_registers);
                printf("\tRead at %d: ", addr);
                for(i = 0; i < nb; i++)
                    printf("%d ", tab_rd_registers[i]);
                printf("\n");
                break;


            case MODBUS_FC_READ_DISCRETE_INPUTS:
                rc = modbus_read_input_bits(ctx, addr, nb, tab_rd_bits);
                printf("modbus_read_input_bits() at %d: ", addr);
                for(i = 0; i < nb; i++)
                    printf("%d ", tab_rd_bits[i]);
                printf("\n");
                break;

            case MODBUS_FC_READ_INPUT_REGISTERS:
                rc = modbus_read_input_registers(ctx, addr, nb, tab_rd_registers);
                printf("modbus_read_input_registers() at %d: ", addr);
                for(i = 0; i < nb; i++)
                    printf("%d ", tab_rd_registers[i]);
                printf("\n");
                break;

            case MODBUS_FC_MASK_WRITE_REGISTER:
                rc = 0;
                printf("Not implemented yet\n\n");
                break;

            default:
                rc = 0;
                printf("Unknown MB-Code: %d\n\n", code);
                continue;
        }

        if (rc != nb) {
            printf("ERROR modbus: %d <> %d, MB-Error: %d \n",
                   rc, nb, errno - MODBUS_ENOBASE);
        }
    }
    return 0;
}
