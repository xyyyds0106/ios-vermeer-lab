/* SPDX-License-Identifier: MIT */
#ifndef VERMEER_BT_UART_SERVICE_H
#define VERMEER_BT_UART_SERVICE_H
#include "vermeer_bt_uart_gpi.h"
/* Permanent provider, called once by reviewed platform owner. This registers
 * the non-Skywalk UART control endpoint, not the default .sk endpoint.
 * It neither starts an image nor changes bluetoothd configuration.
 */
int vermeer_bt_uart_service_start(struct vbug *, unsigned baud);
/* True before initial connect; false forever after disconnect/service fault.
 * Start transfers permanent serialized transport ownership and wraps the
 * provider callbacks, so asynchronous close retires I/O at its next boundary.
 */
int vermeer_bt_uart_service_live(void);
#define VERMEER_BT_UART_CONTROL "com.apple.uart.k70.bluetooth"
#endif
