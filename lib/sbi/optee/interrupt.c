/*
 * Copyright (c) 2022 Nuclei System Technology.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <sbi/sbi_scratch.h>
#include <sbi/riscv_io.h>
#include <sbi_utils/irqchip/plic.h>
#include "sm.h"

#define MODE_M	0
#define MODE_S	1

/*
 * plic_secure_int fill with  hartid and secure interrupt number,
 * secure int format as (hartid | intr), hartid and intr are 16bit width.
 * plic_secure_int[0] indicate total secure interrupt counter
 */
#define MAX_SECURE_INT	16

extern void* irqchip_plic_get_pd(void);

/* high16:cpu id,low16:interrupt number */
static unsigned int plic_secure_int[MAX_SECURE_INT + 1];

static int plic_is_sec_interrupt(unsigned int intr)
{
	int i;

	for(i = 0; i < plic_secure_int[0]; i++)
		if ((intr == (plic_secure_int[i+1] & 0xFFFF)) && (current_hartid() == (plic_secure_int[i+1] >> 16)))
			return true;
	return false;
}

int plic_register_sec_interrupt(unsigned int intr)
{
	unsigned long offset;
	int cnt;
	int hartid;
	int i;
	struct plic_data* pd;

	pd = irqchip_plic_get_pd();
	if (pd == NULL || intr > pd->num_src)
		return false;

	cnt = plic_secure_int[0];
	if (cnt >= MAX_SECURE_INT)
		return false;
	/* check if intr have been registered */
	for (i = 0 ; i < cnt ; i++)
		if ((plic_secure_int[i + 1] & 0xFFFF) == intr)
			return true;
	hartid = current_hartid();
	/* register a new secure interrupt */
	plic_secure_int[cnt + 1] = (hartid << 16) | intr;
	/* update secure total number */
	plic_secure_int[0] = cnt + 1;

	/* set priotity to 1 */
	offset = 4 * intr;
	*(volatile unsigned int *)(pd->addr + offset) = 1;

	/* set Priority threshold for current M/S context */
	offset = 0x200000 + 0x2000 * hartid;
	*(volatile unsigned int *)(pd->addr + offset) = 0;
	offset = 0x200000 + 0x1000 + 0x2000 * hartid;
	*(volatile unsigned int *)(pd->addr + offset) = 0;

	/*
	 * set enable mode to S mode, because this function
	 * only should be called by OPTEE
	 */
	offset = 0x2000 + 0x80 + 0x100 * hartid +
			4 * (intr >> 5);
	*(volatile unsigned int *)(pd->addr + offset) |=
			1 << (intr & 0x1F);

	return true;
}

static int __plic_intr_enabled(unsigned int intr, int mode)
{
	unsigned int hartid = current_hartid();
	unsigned int offset;
	unsigned int val;
	struct plic_data *plic = irqchip_plic_get_pd();

	if (mode == MODE_M) {
		/* Check M-Mode interrupt enable */
		offset = 0x2000 + 0x80 * 2 * hartid +
				4 * (intr >> 5);
		val = (*(volatile unsigned int *)(plic->addr + offset));
		if (val & (1 << (intr & 0x1F)))
			return 1; /* already enabled */
	} else if (mode == MODE_S){
		/* Check S-Mode interrupt enable */
		offset = 0x2000 + 0x80 +
				0x80 * 2 * hartid + 4 * (intr >> 5);
		val = (*(volatile unsigned int *)(plic->addr + offset));
		if (val & (1 << (intr & 0x1F)))
			return 1; /* already enabled */
	}

	return 0;
}

static int plic_intr_enabled(unsigned int intr)
{
	return __plic_intr_enabled(intr, MODE_M) |
		__plic_intr_enabled(intr, MODE_S);
}
/*
 * mode:0:M-Mode, 1:S-Mode
 */
static int plic_intr_set_enable_mode(unsigned int intr, int mode)
{
	unsigned int hartid = current_hartid();
	unsigned int offset;
	struct plic_data *plic = irqchip_plic_get_pd();

	if (mode == MODE_M) {
		/* Set M-Mode interrupt enable */
		offset = 0x2000 + 0x80 * 2 * hartid +
				4 * (intr >> 5);
		(*(volatile unsigned int *)(plic->addr + offset)) |=
				(1 << (intr & 0x1F));
		/* Clear S-Mode interrupt enable */
		offset = 0x2000 + 0x80 +
				0x80 * 2 * hartid + 4 * (intr >> 5);
		(*(volatile unsigned int *)(plic->addr + offset)) &=
				~(1 << (intr & 0x1F));
	} else if (mode == MODE_S){
		/* Set S-Mode interrupt enable */
		offset = 0x2000 + 0x80 +
				0x80 * 2 * hartid + 4 * (intr >> 5);
		(*(volatile unsigned int *)(plic->addr + offset)) |=
				(1 << (intr & 0x1F));
		/* Clear M-Mode interrupt enable */
		offset = 0x2000 + 0x80 * 2 * hartid +
				4 * (intr >> 5);
		(*(volatile unsigned int *)(plic->addr + offset)) &=
				~(1 << (intr & 0x1F));
	} else {
		sbi_printf("%s Mode:%d parameter err\n",__func__, mode);
		return -1;
	}

	return 0;
}

/**
 * @brief switch interrupt enable mode
 * @param next_state, SECURE or NON_SECURE
 */
void plic_intr_switch_enable_mode(int next_state)
{
	int i;
	struct plic_data *plic = irqchip_plic_get_pd();

	if (next_state == SECURE) {
		for(i = 1; i <= plic->num_src; i++) {
			if (plic_intr_enabled(i)) {
				if (plic_is_sec_interrupt(i)) {
					/* config secure interrupt to S-Mode */
					plic_intr_set_enable_mode(i, MODE_S);
				} else {
					/* config non-secure interrupt to M-Mode */
					plic_intr_set_enable_mode(i, MODE_M);
				}
			}
		}
	} else {
		for(i = 1; i <= plic->num_src; i++) {
			if (plic_intr_enabled(i)) {
				if (plic_is_sec_interrupt(i)) {
					/* config secure interrupt to M-Mode */
					plic_intr_set_enable_mode(i, MODE_M);
				} else {
					/* config non-secure interrupt to S-Mode */
					plic_intr_set_enable_mode(i, MODE_S);
				}
			}
		}
	}
}

/*
 * claim M-Mode plic interrupt,
 * suppose hart0 mapping to context0,hart1 to context2
 */
u32 plic_intr_claim(void)
{
	struct plic_data *plic = irqchip_plic_get_pd();
	unsigned int hartid = current_hartid();
	volatile void *plic_claim;

	if (!plic)
		return 0;

	plic_claim = (void *)plic->addr +
			0x200000 + 0x2000 * hartid + 4;

	return readl(plic_claim);
}

void plic_intr_set_pending(int source)
{
	u32 val;
	struct plic_data *plic = irqchip_plic_get_pd();
	volatile void *plic_pending = (void *)plic->addr +
			0x1000 + 4 * (source >> 5);

	val = readl(plic_pending);
	val |= 1 << (source & 0x1F);
	writel(val, plic_pending);
}