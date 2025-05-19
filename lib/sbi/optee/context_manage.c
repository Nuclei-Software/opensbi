/*
 * Copyright (c) 2022 Nuclei System Technology.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <sbi/riscv_asm.h>
#include <sbi/sbi_string.h>
#include <sbi/sbi_scratch.h>
#include "context_manage.h"
#include "sm.h"

extern void plic_intr_switch_enable_mode(int next_state);

void *next_cpu_context_ptr[PLATFORM_CORE_COUNT];

/* Normal cpu context */
cpu_context_t psci_ns_context[PLATFORM_CORE_COUNT];

void switch_vector_sec(void)
{
	extern void _trap_handler_sec();
	csr_write(CSR_MTVEC, &_trap_handler_sec);
}

void switch_vector_normal(void)
{
	extern void _trap_handler();
	csr_write(CSR_MTVEC, &_trap_handler);
}

/*******************************************************************************
 * The following function initializes the cpu_context 'ctx' for
 * first use, and sets the initial entrypoint state as specified by the
 * entry_point_info structure.
 *
 * The security state to initialize is determined by the SECURE attribute
 * of the entry_point_info.
 *
 *
 * To prepare the register state for entry call cm_prepare_el3_exit() and
 * el3_exit(). For Secure-EL1 cm_prepare_el3_exit() is equivalent to
 * cm_el1_sysregs_context_restore().
 ******************************************************************************/
void cm_setup_context(cpu_context_t *ctx, const entry_point_info_t *ep)
{
	assert(ctx != NULL);

	/* Clear any residual register values from the context */
	sbi_memset(ctx, 0, sizeof(*ctx));

	ctx->sec_attr	= (ep->sec_attr == SECURE) ? SECURE : NON_SECURE;
	ctx->gp_regs.mepc	= ep->pc;
	ctx->gp_regs.mstatus	= (1 << MSTATUS_MPP_SHIFT);
	ctx->gp_regs.a0 = ep->arg0;
	ctx->gp_regs.a1 = ep->arg1;
	ctx->gp_regs.a2 = ep->arg2;
	ctx->gp_regs.a3 = ep->arg3;
	ctx->gp_regs.a4 = ep->arg4;
	ctx->gp_regs.a5 = ep->arg5;
	ctx->gp_regs.a6 = ep->arg6;
	ctx->gp_regs.a7 = ep->arg7;
	if (ep->sec_attr == SECURE) {
		ctx->s_csrs.sie = 0;
		ctx->s_csrs.scounteren = csr_read(CSR_SCOUNTEREN);
	}
	else
		ctx->s_csrs.sie = csr_read(CSR_SIE);
}

/* from sbi trap regs to cpu context */
void cm_gpregs_context_save(uint32_t security_state,
			    const struct sbi_trap_regs *trap_reg)
{
	cpu_context_t *ctx;

	ctx = cm_get_context(security_state);
	assert(ctx != NULL);

	sbi_memcpy(&ctx->gp_regs, trap_reg, sizeof(struct sbi_trap_regs));
}

void cm_sysregs_context_save(uint32_t security_state)
{
	cpu_context_t *ctx;

	ctx = cm_get_context(security_state);
	assert(ctx != NULL);

#define SAVE_SMODE_CSR(csrname) ctx->s_csrs.csrname = csr_read(csrname);

	SAVE_SMODE_CSR(sstatus);
	// These only exist with N extension.
	//LOCAL_SWAP_CSR(sedeleg);
	//LOCAL_SWAP_CSR(sideleg);
	SAVE_SMODE_CSR(sie);
	SAVE_SMODE_CSR(stvec);
	SAVE_SMODE_CSR(scounteren);
	SAVE_SMODE_CSR(sscratch);
	SAVE_SMODE_CSR(sepc);
	SAVE_SMODE_CSR(scause);
	SAVE_SMODE_CSR(sbadaddr);
	SAVE_SMODE_CSR(sip);
	SAVE_SMODE_CSR(satp);
#undef SAVE_SMODE_CSR
}

void cm_sysregs_context_restore(uint32_t security_state)
{
	cpu_context_t *ctx;

	ctx = cm_get_context(security_state);
	assert(ctx != NULL);

#define RESTORE_SMODE_CSR(csrname) csr_write(csrname, ctx->s_csrs.csrname);

	RESTORE_SMODE_CSR(sstatus);
	// These only exist with N extension.
	//LOCAL_SWAP_CSR(sedeleg);
	//LOCAL_SWAP_CSR(sideleg);
	RESTORE_SMODE_CSR(sie);
	RESTORE_SMODE_CSR(stvec);
	RESTORE_SMODE_CSR(scounteren);
	RESTORE_SMODE_CSR(sscratch);
	RESTORE_SMODE_CSR(sepc);
	RESTORE_SMODE_CSR(scause);
	RESTORE_SMODE_CSR(sbadaddr);
	RESTORE_SMODE_CSR(sip);
	RESTORE_SMODE_CSR(satp);
#undef RESTORE_SMODE_CSR
}

#ifdef CFG_WITH_VFP
extern void vfp_save_extension_regs(struct vfp_reg regs[VFP_NUM_REGS]);
extern void vfp_restore_extension_regs(struct vfp_reg regs[VFP_NUM_REGS]);

void cm_vfp_context_save(uint32_t security_state)
{
	cpu_context_t *ctx;

	ctx = cm_get_context(security_state);
	assert(ctx != NULL);

	if (csr_read(CSR_SSTATUS) & SSTATUS_SD) {
		ctx->vfp_regs.fcsr = csr_read(CSR_FCSR);
		vfp_save_extension_regs(ctx->vfp_regs.reg);
	}
}

void cm_vfp_context_restore(uint32_t security_state)
{
	cpu_context_t *ctx;

	ctx = cm_get_context(security_state);
	assert(ctx != NULL);

	if (ctx->s_csrs.sstatus & SSTATUS_FS) {
		csr_write(CSR_FCSR, ctx->vfp_regs.fcsr);
		vfp_restore_extension_regs(ctx->vfp_regs.reg);
	}
}
#else
void cm_vfp_context_save(uint32_t security_state)
{
}

void cm_vfp_context_restore(uint32_t security_state)
{
}
#endif

void cm_set_mepc(uint32_t security_state, uintptr_t entrypoint)
{
	cpu_context_t *ctx;

	ctx = cm_get_context(security_state);
	assert(ctx != NULL);

	/* update state so that MRET jumps to the correct entry */
	ctx->gp_regs.mepc = entrypoint;
}
/*******************************************************************************
 * The following function initializes the cpu_context for a CPU specified by
 * its `cpu_idx` for first use, and sets the initial entrypoint state as
 * specified by the entry_point_info structure.
 ******************************************************************************/
void cm_init_context_by_index(unsigned int cpu_idx,
			      const entry_point_info_t *ep)
{
	cpu_context_t *ctx;
	ctx = cm_get_context_by_index(cpu_idx, ep->sec_attr);
	cm_setup_context(ctx, ep);
}

/*******************************************************************************
 * The following function initializes the cpu_context for the current CPU
 * for first use, and sets the initial entrypoint state as specified by the
 * entry_point_info structure.
 ******************************************************************************/
void cm_init_my_context(const entry_point_info_t *ep)
{
	cm_init_context_by_index(current_hartid(), ep);
}

void *cm_get_context(uint32_t security_state)
{
	return cm_get_context_by_index(current_hartid(), security_state);
}

/*******************************************************************************
 * This function returns a pointer to the most recent 'cpu_context' structure
 * for the CPU identified by `cpu_idx` that was set as the context for the
 * specified security state. NULL is returned if no such structure has been
 * specified.
 ******************************************************************************/
void *cm_get_context_by_index(unsigned int cpu_idx, unsigned int security_state)
{
	void *ret = NULL;

	if (security_state == SECURE)
		ret = &opteed_sp_context[cpu_idx].cpu_ctx;
	else if (security_state == NON_SECURE)
		ret = &psci_ns_context[cpu_idx];

	return ret;
}

/*******************************************************************************
 * This function sets the pointer to the current 'cpu_context' structure for the
 * specified security state for the CPU identified by CPU index.
 ******************************************************************************/
void cm_set_context_by_index(unsigned int cpu_idx, void *context,
			     unsigned int security_state)
{
	/* do nothing, because we use opteed_sp_context as secure cpu ctx
 * psci_ns_context as non-secure cpu ctx
 */
}

void cm_set_next_context(void *context)
{
	assert(context != NULL);
	next_cpu_context_ptr[current_hartid()] = context;
}

void *cm_get_next_context(void)
{
	return next_cpu_context_ptr[current_hartid()];
}

/*******************************************************************************
 * This function is used to program the context that's used for exception
 * return. This initializes the SP_EL3 to a pointer to a 'cpu_context' set for
 * the required security state
 ******************************************************************************/
void cm_set_next_eret_context(uint32_t security_state)
{
	cpu_context_t *ctx;

	ctx = cm_get_context(security_state);
	assert(ctx != NULL);

	cm_set_next_context(ctx);

	/* config pmp according to security_state*/
	if (security_state == SECURE) {
		/* all interrupt trap to M mode
		 * exception ?
		 */
		plic_intr_switch_enable_mode(SECURE);
		switch_vector_sec();
		osm_pmp_set(PMP_NO_PERM);
		shm_pmp_set(PMP_ALL_PERM);
		plicm_pmp_set(PMP_ALL_PERM);
#ifdef SEC_TIMER_TEST
		timerm_pmp_set(PMP_ALL_PERM);
#endif
#ifdef HSM_MAILBOX
		mailboxm_pmp_set(PMP_ALL_PERM);
#endif
		teem_pmp_set(PMP_ALL_PERM);
	} else if (security_state == NON_SECURE) {
		/* S/T int trap delegate to S mode,
		 * Ext int trap to M mode.
		 * Because secure Ext need to transfer to opteeos,
		 * non-secure Ext redirect to linux os
		 */
		plic_intr_switch_enable_mode(NON_SECURE);
		switch_vector_normal();
		teem_pmp_set(PMP_NO_PERM);
		shm_pmp_set(PMP_ALL_PERM);
		plicm_pmp_set(PMP_ALL_PERM);
#ifdef SEC_TIMER_TEST
		timerm_pmp_set(PMP_ALL_PERM);
#endif
#ifdef HSM_MAILBOX
		mailboxm_pmp_set(PMP_ALL_PERM);
#endif
		osm_pmp_set(PMP_ALL_PERM);
	} else {
		sbi_printf("not support secure state!\n");
		sbi_hart_hang();
	}
}

void __noreturn cm_restore_next_context(uint32_t security_state, u64 new_epc)
{
	cpu_context_t *next;
	/*restore sysreg*/
	cm_sysregs_context_restore(security_state);
	/*restore vfp regs*/
	cm_vfp_context_restore(security_state);
	/*restore gpreg then mret*/
	next = cm_get_next_context();
	cm_restore_context_lowlevel(next, new_epc);
}
