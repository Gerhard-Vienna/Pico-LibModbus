/*
 * Copyright © Gerhard Schiller 2025, <gerhard.schiller@pm.me>
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This example shows the basic structure of a ModBus server on a Pico-W.
 *
 * Use libmodbus/tests/test-client-cli as the client to test this server.
 * See README.md for details.
 */

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/multicore.h"

#include "wifi.h"
#include <modbus.h>

/* MODBUS data modell for the example server:
 *
 * Input register (libmodbus mapping: mb_mapping->tab_input_registers)
 *  0       The LEDs on-time in ms
 *
 * Holding register (libmodbus mapping: mb_mapping->tab_registers)
 *  nada
 *
 * Discrete inputs (libmodbus mapping: mb_mapping->tab_input_bits)
 *  Nada
 *
 * Coils:(modbus mapping: mb_mapping->tab_bits)
 *  0      Write a "1" into it to switch the LED on, 0 for off
 *  1      Write a "1" to enable debugging output, "0" to disable
 */

// The libmodbus context
modbus_t *ctx = NULL;

// The four Modbus tabels
modbus_mapping_t *mb_mapping = NULL;

// The layout of the Modbus tabels for this server:

// 1 byte in the "bits" table (Coils) to control the LED
// NOTE: libModbus stores each 'bit', or coil in Modbus terminology,
// as a uint8_t.t)
#define NB_BITS				2
// None in the "input_bits" table (Discrete inputs)
#define NB_INPUT_BITS		0
// None in "Registers" table (Holding Registers)
#define NB_REGISTERS		0
// 1 word (2 bytes) in the "input_registers" table (Input Registers)
// to store the LED on-time in ms
#define NB_INPUT_REGISTERS	1

// Wi-Fi initialization
void init_wifi() {
	// Connect to Wi-Fi
	printf("Connecting to WiFi... ");
	if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000)) {
		printf("\b\b\b\b, FAILED TO CONNECT.\n");
		return;
	} else {
		printf("\b\b\b\b, connected.\n");
	}
}

// Initialize network with static IP configuration
void init_network() {
	ip4_addr_t ip, gw, mask;

	ip4addr_aton(SERVER_PICO_IP, &ip);
	ip4addr_aton(NETMASK, &mask);
	ip4addr_aton(GATEWAY, &gw);

	if (cyw43_arch_init()) {
		printf("Network failed to initialise\n");
		return;
	}
	cyw43_arch_enable_sta_mode();

	netif_set_addr(netif_default, &(ip), &(mask), &(gw));
	netif_set_up(netif_default);
	printf("Using static IP, set:\n");
	printf("\tIP Address: %s\n",
		   ip4addr_ntoa(netif_ip4_addr(netif_default)));
	printf("\tNet mask: %s\n",
		   ip4addr_ntoa(netif_ip4_netmask(netif_default)));
	printf("\tDefault gateway: %s\n",
		   ip4addr_ntoa(netif_ip4_gw(netif_default)));
}

void init_MbServer(void)
{

	// Allocate memory for the 4 Modbus tabels.
	mb_mapping =
		modbus_mapping_new(NB_BITS, NB_INPUT_BITS,
					NB_REGISTERS, NB_INPUT_REGISTERS);
	if (mb_mapping == NULL) {
		printf("Failed to allocate the mapping: %s\n",
				modbus_strerror(errno));
		return;
	}

	// Start the server and create the libmodbus context.
	ctx = tcp_server_init(502);
	// modbus_set_debug(ctx, TRUE);

	if (ctx == NULL) {
		printf("Unable to allocate libmodbus context\n");
		return;
	}
}

// This is the task that runs on Core 1.
// It provides the complete functionality of a client.
void runMbServer(void)
{
	modbus_message_t mb_msg;

	uint8_t request[MODBUS_TCP_MAX_ADU_LENGTH];
	int clientID;
	int request_len;
	int rc;

	//The usual endless loop.
	while(1){
		// Iterate over all possible connections to check whether
		// a clients state has changed.
		for (clientID = 0; clientID < MAX_PEERS; clientID++) {
			if(!modbus_is_connected(clientID)){
				// Either no connection with this ID or the
				// connection is down...
				continue;
			}

			// IMPORTANT: Do not omit this line.
			// It is essential that modbus_set_connectionID() is called
			// before any other routines that use ctx becouse it sets the
			// ctx-context to the desired client.
			modbus_set_connectionID(ctx, clientID);

			// Get the clients status
			rc = modbus_client_status(clientID);
			// if(rc == 0){
			if(rc == 0){
				// No request in  queue
				continue;
			}

			// If we have reached this point (rc > 0), it means the client has
			// sent a request, with rc being the number of bytes in the request.
			// Termination of the connection by the client (rc < 0) is handled by
			// modbus_receive().

			// Get the request
			if((request_len = modbus_receive(ctx, request)) < 0){
				printf("runMbServer, client %d, error: %s\n",
					   clientID, strerror(errno));
				continue;
			}

			// Send the reply
			rc = modbus_reply(ctx, request, request_len, mb_mapping);
			if(rc < 0){
				printf("runMbServer, client %d, error: %s\n",
					   clientID, strerror(errno));
				continue;
			}

			// Check whether the request has modified any data in one of
			// the Modbus tables. In other words, check if it was a write
			// (or read/write) operation rather than a read-only one.
			// If so, push a message for core 0 into the FIFO.
			modbus_notify_if_write(ctx, request, &mb_msg);
		}
	}
}

void main()
{
	// In this structure, core 1 informs the main loop (core 0)
	// of any changes made by the client.
	modbus_message_t *p_mb_msg;

	// Timestamp for when the LED was turned on.
	uint32_t led_on_start;

	stdio_init_all();
	printf("pico-hello-server\n\n");

	// Initialize the network
	init_network();

	// Initialize Wi-Fi
	init_wifi();

	// Initialize the ModBus-Server
	init_MbServer();

	// Run the ModBus-Server
	multicore_launch_core1(runMbServer);

	for(;;){
		// Check wether a client has modified any data in one of the
		// Modbus tables (p_mb_msg != NULL)
		if(p_mb_msg = modbus_write_notify()){
			printf("MbServer notified: function:%d, address:%d, count:%d\n",
					   p_mb_msg->func, p_mb_msg->addr, p_mb_msg->count);

			switch (p_mb_msg->func) {
				case MODBUS_FC_WRITE_SINGLE_COIL:
				case MODBUS_FC_WRITE_MULTIPLE_COILS:
					// Lock the Modbus tables

					// This is for demonstration only...
					printf("%d COIL(S) modified:\n", p_mb_msg->count);
					for(int i = 0; i <  p_mb_msg->count; i++){
						modbus_tcp_mapping_lock(ctx);
						printf("\t0x%02X @ 0x%02X\n",
								mb_mapping->tab_bits[p_mb_msg->addr + i],
								p_mb_msg->addr + i);
						modbus_tcp_mapping_unlock(ctx);
					}

					// The real job
					if(p_mb_msg->addr == 0){
						// A change in the state of the LED was requested
						// by the client.

						// Get the requested state
						modbus_tcp_mapping_lock(ctx);
						int ledStatus = mb_mapping->tab_bits[0];
						modbus_tcp_mapping_unlock(ctx);

						if(ledStatus == 1){
							cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
							led_on_start = time_us_32();
							printf("LED on requested\n");
						}
						else{
							cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
							printf("LED off requested\n");
						}
					}
					if(p_mb_msg->addr == 1){
						// A change for debug enable was requested
						// by the client.
						modbus_tcp_mapping_lock(ctx);
						int debugState = mb_mapping->tab_bits[1];
						modbus_tcp_mapping_unlock(ctx);
						if(debugState == 1){
							printf("Debug on requested\n");
							modbus_set_debug(ctx, TRUE);
						}
						else{
							printf("Debug off requested\n");
							modbus_set_debug(ctx, FALSE);
						}
					}
					break;

				// In this example no registers are modified,
				// so it is for demonstration only...
				case MODBUS_FC_WRITE_SINGLE_REGISTER:
				case MODBUS_FC_WRITE_MULTIPLE_REGISTERS:
				case MODBUS_FC_WRITE_AND_READ_REGISTERS:
					printf("%d REGISTER(S) modified:\n", p_mb_msg->count);
					for(int i = 0; i <  p_mb_msg->count; i++){
						modbus_tcp_mapping_lock(ctx);
						printf("\t%d at 0x%02X\n",
							mb_mapping->tab_registers[p_mb_msg->addr + i],
							p_mb_msg->addr + i);
						modbus_tcp_mapping_unlock(ctx);
					}
					break;

				default:
					// Should never happen
					printf("Unknown write-function %d\n", p_mb_msg->func);
			}
		}

		// The actual work can be started once it has been determined
		// whether a client's requests have resulted in a change of
		// status for this server.
		if(cyw43_arch_gpio_get(CYW43_WL_GPIO_LED_PIN)){
			// LED is on, update the on-time
			modbus_tcp_mapping_lock(ctx);
			mb_mapping->tab_input_registers[0] =
				(time_us_32() - led_on_start) / 1000;
			modbus_tcp_mapping_unlock(ctx);
		}
		else{
			// LED is off, set on-time to 0
			modbus_tcp_mapping_lock(ctx);
			mb_mapping->tab_input_registers[0] = 0;
			modbus_tcp_mapping_unlock(ctx);
		}

		sleep_ms(10);
	}
}
