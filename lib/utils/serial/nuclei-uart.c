/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2019 Western Digital Corporation or its affiliates.
 * Modified to support Nuclei UART
 *
 * Authors:
 *   Anup Patel <anup.patel@wdc.com>
 */

#include <sbi/riscv_io.h>
#include <sbi/sbi_console.h>
#include <sbi_utils/serial/nuclei-uart.h>

/* clang-format off */

#define UART_REG_TXFIFO		0
#define UART_REG_RXFIFO		1
#define UART_REG_TXCTRL		2
#define UART_REG_RXCTRL		3
#define UART_REG_IE		4
#define UART_REG_IP		5
#define UART_REG_DIV		6
#define UART_REG_SETUP	7

#define UART_TXFIFO_FULL	(1U << 14)
#define UART_RXFIFO_EMPTY	(1U << 15)

#define UART_RXFIFO_DATA	0x000000ff
#define UART_TXCTRL_TXEN	0x1
#define UART_RXCTRL_RXEN	0x1
#define UART_SETUP_8BNP		0x30

#define CFG_STOP_BIT_MASK	(0x3 << 1)
#define CFG_STOP_BIT_1BIT	(0x1 << 1)


/* clang-format on */

static volatile char *uart_base;
static u32 uart_in_freq;
static u32 uart_baudrate;

/**
 * Find minimum divisor divides in_freq to max_target_hz;
 *
 * f_baud = f_in / (div + 1) => div = (f_in / f_baud) - 1
 * The nearest integer solution requires rounding up as to not exceed max_target_hz.
 * div  = ceil(f_in / f_baud) - 1
 *	= floor((f_in - 1 + f_baud) / f_baud) - 1
 * This should not overflow as long as (f_in - 1 + f_baud) does not exceed
 * 2^32 - 1, which is unlikely since we represent frequencies in kHz.
 */
static inline unsigned int uart_min_clk_divisor(uint64_t in_freq,
						uint64_t max_target_hz)
{
	uint64_t quotient = (in_freq + max_target_hz - 1) / (max_target_hz);

	/* Avoid underflow */
	if (quotient == 0)
		return 0;
	else
		return quotient - 1;
}

static u32 get_reg(u32 num)
{
	return readl(uart_base + (num * 0x4));
}

static void set_reg(u32 num, u32 val)
{
	writel(val, uart_base + (num * 0x4));
}

static void nuclei_uart_putc(char ch)
{
	while (get_reg(UART_REG_IP) & UART_TXFIFO_FULL)
		;

	set_reg(UART_REG_TXFIFO, ch);
}

static int nuclei_uart_getc(void)
{
	u32 ret;

	do {
		ret = get_reg(UART_REG_IP);
	} while (ret & UART_RXFIFO_EMPTY);

	ret = get_reg(UART_REG_RXFIFO);

	return ret & UART_RXFIFO_DATA;
}

static struct sbi_console_device nuclei_console = {
	.name = "nuclei_uart",
	.console_putc = nuclei_uart_putc,
	.console_getc = nuclei_uart_getc
};

int nuclei_uart_init(unsigned long base, u32 in_freq, u32 baudrate)
{
	unsigned int txctl_val = 0;

	uart_base     = (volatile char *)base;
	uart_in_freq  = in_freq;
	uart_baudrate = baudrate;

	/* Configure baudrate */
	if (in_freq && baudrate)
		set_reg(UART_REG_DIV, uart_min_clk_divisor(in_freq, baudrate));

	/* Disable interrupts */
	set_reg(UART_REG_IE, 0);

	/* Enable TX , config 1bit stop bit*/
	txctl_val = get_reg(UART_REG_TXCTRL);
	txctl_val &= ~CFG_STOP_BIT_MASK;
	txctl_val |= CFG_STOP_BIT_1BIT | UART_TXCTRL_TXEN;
	set_reg(UART_REG_TXCTRL, txctl_val);

	/* Enable Rx */
	set_reg(UART_REG_RXCTRL, UART_RXCTRL_RXEN);

	/* Disable parity bit generation, disable clean fifo, bit_length 8bit */
	set_reg(UART_REG_SETUP, UART_SETUP_8BNP);

	sbi_console_set_device(&nuclei_console);

	return 0;
}
