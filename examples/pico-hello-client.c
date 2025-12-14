/*
 * Copyright © Gerhard Schiller 2025, <gerhard.schiller@pm.me>
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This example shows the basic structure of a ModBus client on a Pico-W.
 *
 * Use libmodbus/tests/hello-server as the server to test this client.
 * See README.md for details.
 */

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "wifi.h"
#include <modbus.h>

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

	ip4addr_aton(CLIENT_IP, &ip);
	ip4addr_aton(NETMASK, &mask);
	ip4addr_aton(GATEWAY, &gw);

	if (cyw43_arch_init()) {
		printf("failed to initialise\n");
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

void main()
{
	modbus_t *ctx = NULL;
	int serverID;
	uint16_t val_in, val_out;
	int rc;

	stdio_init_all();
	printf("pico-hello-client\n\n");

	// Initialize the network with a static IP
	init_network();

	// Initialize Wi-Fi
	init_wifi();

	// Initialize modbus
	ctx = tcp_client_init();
#if PICO_TCP_DEBUG
	modbus_set_debug(ctx, TRUE);
#else
	modbus_set_debug(ctx, FALSE);
#endif

	// It is the client's responsibility to re-establish an interrupted
	// connection.
	modbus_set_error_recovery(ctx,
							  MODBUS_ERROR_RECOVERY_LINK |
							  MODBUS_ERROR_RECOVERY_PROTOCOL);


	// Initialize the clients connection to the server
	serverID = tcp_new_client(SERVER_WS_IP, 502);

	// IMPORTANT: Do not omit this line.
	// It is essential that modbus_set_connectionID() is called
	// before any other routines that use ctx becouse it sets the
	// ctx-context to the desired client.
	modbus_set_connectionID(ctx, serverID);

#define WAIT_fOR_SERVER_ONLINE
#ifdef WAIT_fOR_SERVER_ONLINE
	while(1){
		if(modbus_connect(ctx) < 0){
			printf("connect error: %s\n", strerror(errno));
		}
		if(modbus_is_connected(serverID)){
			break;
		}
		else{
			printf("will try again to connect in 5 sec\n");
			sleep_ms(5000);
		}
	}
	modbus_flush(ctx);
#endif

	for(;;){
		val_in = (rand() % 9) + 1;
		rc = modbus_write_register(ctx, 0, val_in);
		printf("Sent %d, ", val_in);
		if (rc != 1) {
			printf("ERROR modbus_write_register (%d)\n", rc);
		}
		else {
			 rc = modbus_read_input_registers(ctx, 0, 1, &val_out);
			 if (rc > 0) {
				 printf("received %d\n", val_out);
			 }
			 else{
				 printf("ERROR modbus_read_input_registers (%d)\n", rc);
			}
		}
		sleep_ms(3000);
	}
}
