//******************************************************************************
// Copyright (c) 2018, The Regents of the University of California (Regents).
// All Rights Reserved. See LICENSE for license details.
//------------------------------------------------------------------------------
#include "sm.h"
#include "pmp.h"
#include <sbi/sbi_string.h>
#include <sbi/riscv_locks.h>
#include <sbi/riscv_barrier.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_hart.h>
#include <sbi/riscv_asm.h>
#include <sbi/sbi_ecall.h>
#include <opteed_private.h>
#include <sbi_utils/irqchip/plic.h>
#include <sbi_utils/fdt/fdt_helper.h>

extern void* irqchip_plic_get_pd(void);
extern int fdt_node_offset_by_compatible(const void *fdt,
			int startoffset, const char *compatible);
extern struct sbi_ecall_extension ecall_optee;
static int sm_init_done = 0;
static int sm_region_id = 0, os_region_id = 0;
int tee_region_id = 0, shm_region_id = 0;
int plicm_region_id = 0,timer_region_id = 0;
int mailbox_region_id = 0;

int osm_pmp_set(uint8_t perm)
{
	return pmp_set_keystone(os_region_id, perm);
}

int teem_pmp_set(uint8_t perm)
{
	return pmp_set_keystone(tee_region_id, perm);
}

int shm_pmp_set(uint8_t perm)
{
	return pmp_set_keystone(shm_region_id, perm);
}

int plicm_pmp_set(uint8_t perm)
{
	return pmp_set_keystone(plicm_region_id, perm);
}

int timerm_pmp_set(uint8_t perm)
{
	return pmp_set_keystone(timer_region_id, perm);
}

int mailboxm_pmp_set(uint8_t perm)
{
	return pmp_set_keystone(mailbox_region_id, perm);
}

int smm_init()
{
	int region = -1;
	int ret	= pmp_region_init_atomic(SMM_BASE, SMM_SIZE, PMP_PRI_TOP,
					    &region, 0);
	if (ret)
		return -1;

	return region;
}

int osm_init()
{
	int region = -1;
	int ret = pmp_region_init_atomic(0, -1UL, PMP_PRI_BOTTOM, &region, 1);
	if (ret)
		return -1;

	return region;
}

int teem_init()
{
	int region = -1;
	int ret = pmp_region_init_atomic(OPTEE_TZDRAM_BASE, OPTEE_TZDRAM_SIZE,
					 PMP_PRI_ANY, &region, 0);
	if (ret)
		return -1;

	return region;
}

int shm_init()
{
	int region = -1;
	int ret	   = pmp_region_init_atomic(OPTEE_SHMEM_BASE, OPTEE_SHMEM_SIZE,
					    PMP_PRI_BOTTOM, &region, 0);
	if (ret)
		return -1;

	return region;
}

int plicm_init()
{
	int region = -1;
	struct plic_data *plic = irqchip_plic_get_pd();

	int ret = pmp_region_init_atomic(plic->addr, OPTEE_PLIC_SIZE,
					 PMP_PRI_ANY, &region, 0);
	if (ret)
		return -1;

	return region;
}

#ifdef SEC_TIMER_TEST
int timerm_init()
{
	int region = -1;
	int ret = pmp_region_init_atomic(OPTEE_TIMER_BASE, OPTEE_TIMER_SIZE,
					 PMP_PRI_ANY, &region, 0);
	if (ret)
		return -1;

	return region;
}
#endif

#ifdef HSM_MAILBOX
int mailboxm_init()
{
	int region = -1;
	int nodeoffset, rc;
	uint64_t reg_addr, reg_size;
	void *fdt = fdt_get_address();

	nodeoffset = fdt_node_offset_by_compatible(fdt, -1, "nuclei,nuclei-mbox");
	if (nodeoffset < 0)
		return -1;
	rc = fdt_get_node_addr_size(fdt, nodeoffset, 0,
				    &reg_addr, &reg_size);
	if (rc < 0 || !reg_addr || !reg_size)
		return -1;

	if (reg_size & (reg_size -1)) {
		sbi_printf("mailbox size is not ^2 aligned, please ensure aligned!\n");
		return -1;
	}
	rc = pmp_region_init_atomic((uintptr_t)reg_addr, reg_size,
					 PMP_PRI_ANY, &region, 0);
	if (rc)
		return -1;

	return region;
}
#endif

void sm_init(bool cold_boot)
{
	// initialize SMM
	if (cold_boot) {
#ifdef ENABLE_TEE_WG
		sbi_printf("[SM](WG) Initializing ... hart [%lx]\n",
			   csr_read(mhartid));
#else
		/* only the cold-booting hart will execute these */
		sbi_printf("[SM] Initializing ... hart [%lx]\n",
			   csr_read(mhartid));
#endif
		sbi_ecall_register_extension(&ecall_optee);

		sm_region_id = smm_init();
		if (sm_region_id < 0) {
			sbi_printf(
				"[SM] failed to create secure monitor pmp entry.\n");
			sbi_hart_hang();
		}

		os_region_id = osm_init();
		if (os_region_id < 0) {
			sbi_printf(
				"[SM] failed to create reeos pmp entry.\n");
			sbi_hart_hang();
		}

		tee_region_id = teem_init();
		if (tee_region_id < 0) {
			sbi_printf(
				"[SM] failed to create teeos pmp entry.\n");
			sbi_hart_hang();
		}

		shm_region_id = shm_init();
		if (shm_region_id < 0) {
			sbi_printf(
				"[SM] failed to create tee_share pmp entry.\n");
			sbi_hart_hang();
		}

		plicm_region_id = plicm_init();
		if (plicm_region_id < 0) {
			sbi_printf(
				"[SM] failed to create plic pmp entry.\n");
			sbi_hart_hang();
		}

#ifdef SEC_TIMER_TEST
		timer_region_id = timerm_init();
		if (timer_region_id < 0) {
			sbi_printf(
				"[SM] failed to create test_timer pmp entry.\n");
			sbi_hart_hang();
		}
#endif

#ifdef HSM_MAILBOX
		/*
		 * mailbox pmp is for communication between host and hsm
		 * only used for nuclei hsm crypto engine.
		 */
		mailbox_region_id = mailboxm_init();

		/* enable optee use CCM cache function */
		#define CCM_SUEN 0x7CE
		#define S_WB_ALL_ENABLE  (1 << 25)
		#define S_INV_ALL_ENABLE (1 << 17)
		#define S_INV_ENABLE     (1 << 9)
		#define S_CCM_ENABLE     (1 << 1)
		csr_write(CCM_SUEN, S_WB_ALL_ENABLE|S_INV_ALL_ENABLE
			| S_INV_ENABLE | S_CCM_ENABLE);
#endif
		sm_init_done = 1;
		mb();
	}

	/* wait until cold-boot hart finishes */
	while (!sm_init_done) {
		mb();
	}

	/* below are executed by all harts */
	pmp_init();
	pmp_set_keystone(sm_region_id, PMP_NO_PERM);
	pmp_set_keystone(os_region_id, PMP_ALL_PERM);
	pmp_set_keystone(tee_region_id, PMP_NO_PERM);

	if (cold_boot) {
		opteed_init();
	}
	sbi_printf("[SM] security monitor has been initialized!\n");

	return;
}
