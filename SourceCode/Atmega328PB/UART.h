#ifndef UART_H
#define UART_H

#include <avr/io.h>


void UART0_Init(void);

bool UART_isRxMsgReady(void);

void UART_ResetRxMsgReadyFlag(void);
void UART_ResetRxMsgIndex(void);
void UART0_SendString(const char *s);

void UART1_Init(void);

void UART1_SendChar(char c);

void UART1_SendString(const char *s);
\

#endif
