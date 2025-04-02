/*
 * Copyright (c) 2013-2017, ARM Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <sbi/riscv_asm.h>
#include <sbi/sbi_string.h>
#include "opteed_private.h"

#define OPTEE_OS_LOAD_ADDR	OPTEE_TZDRAM_BASE
/*******************************************************************************
 * Address of the entrypoint vector table in OPTEE. It is
 * initialised once on the primary core after a cold boot.
 ******************************************************************************/
struct optee_vectors *optee_vector_table;

/*******************************************************************************
 * Array to keep track of per-cpu OPTEE state
 ******************************************************************************/
optee_context_t opteed_sp_context[OPTEED_CORE_COUNT];

static ulong tmp_trap_stack[1024];
static ulong tmp_mscratch[OPTEED_CORE_COUNT];
/*******************************************************************************
 * This function passes control to the OPTEE image (BL32) for the first time
 * on the primary cpu after a cold boot. It assumes that a valid secure
 * context has already been created by opteed_setup() which can be directly
 * used.  It also assumes that a valid non-secure context has been
 * initialised by PSCI so it does not need to save and restore any
 * non-secure state. This function performs a synchronous entry into
 * OPTEE. OPTEE passes control back to this routine through a SMC.
 ******************************************************************************/
int32_t opteed_init(void)
{
	uint32_t linear_id = current_hartid();
	optee_context_t *optee_ctx = &opteed_sp_context[linear_id];
	entry_point_info_t img_entry_point;
	uint64_t rc;
	uint64_t saved_mie;

	/*init cpu context*/
	sbi_memset(psci_ns_context, 0, sizeof(psci_ns_context));
	sbi_memset(opteed_sp_context, 0, sizeof(opteed_sp_context));
	/*
	 * Get information about the OPTEE (BL32) image. Its
	 * absence is a critical failure.
	 */
	sbi_memset(&img_entry_point, 0, sizeof(entry_point_info_t));
	img_entry_point.sec_attr = SECURE;
	/* optee os run addr*/
	img_entry_point.pc = OPTEE_OS_LOAD_ADDR;
	/* optee run param should be line with optee image */
	img_entry_point.arg0 = current_hartid();
	img_entry_point.arg1 = 8*1024*1024;
	img_entry_point.arg2 = FW_JUMP_FDT_ADDR;

	cm_init_my_context(&img_entry_point);

	saved_mie = csr_read(CSR_MIE);
	/*
	 * Init optee MIE
	 * disable all interrupt
	 */
	csr_write(CSR_MIE, 0);
	/*
	 * Arrange for an entry into OPTEE. It will be returned via
	 * OPTEE_ENTRY_DONE case
	 */
	rc = opteed_synchronous_sp_entry(optee_ctx);
	assert(rc != 0);
	/* restore mie of normal world*/
	csr_write(CSR_MIE, saved_mie);

	sbi_memset(&img_entry_point, 0, sizeof(entry_point_info_t));
	/* Next image is non secure */
	img_entry_point.sec_attr = NON_SECURE;
	cm_init_my_context(&img_entry_point);
	cm_set_next_eret_context(NON_SECURE);

	return rc;
}

/*******************************************************************************
 * This function takes an OPTEE context pointer and:
 * 1. Applies the S-EL1 system register context from optee_ctx->cpu_ctx.
 * 2. Saves the current C runtime state (callee saved registers) on the stack
 *    frame and saves a reference to this state.
 * 3. Calls el3_exit() so that the EL3 system and general purpose registers
 *    from the optee_ctx->cpu_ctx are used to enter the OPTEE image.
 ******************************************************************************/
uint64_t opteed_synchronous_sp_entry(optee_context_t *optee_ctx)
{
	uint64_t rc;

	assert(optee_ctx != NULL);
	assert(optee_ctx->c_rt_ctx == 0);

	/* Apply the Secure EL1 system register context and switch to it */
	assert(cm_get_context(SECURE) == &optee_ctx->cpu_ctx);
	cm_sysregs_context_restore(SECURE);
	cm_set_next_eret_context(SECURE);
	/*
	 * use temp scratch space,because current scratch space will be used again
	 * in order to return to this function after optee finished init.
	 * take tmp_trap_stack as scratch space and stack temporarily.
	 */
	tmp_mscratch[current_hartid()] = csr_read(CSR_MSCRATCH);
	csr_write(CSR_MSCRATCH,&tmp_trap_stack[array_size(tmp_trap_stack) - 128]);
	rc = opteed_enter_sp(&optee_ctx->c_rt_ctx);

	return rc;
}


/*******************************************************************************
 * This function takes an OPTEE context pointer and:
 * 1. Saves the S-EL1 system register context tp optee_ctx->cpu_ctx.
 * 2. Restores the current C runtime state (callee saved registers) from the
 *    stack frame using the reference to this state saved in opteed_enter_sp().
 * 3. It does not need to save any general purpose or EL3 system register state
 *    as the generic smc entry routine should have saved those.
 ******************************************************************************/
void opteed_synchronous_sp_exit(optee_context_t *optee_ctx, uint64_t ret)
{
	assert(optee_ctx != NULL);
	/* Save the Secure EL1 system register context */
	assert(cm_get_context(SECURE) == &optee_ctx->cpu_ctx);
	cm_sysregs_context_save(SECURE);

	assert(optee_ctx->c_rt_ctx != 0);
	csr_write(CSR_MSCRATCH,tmp_mscratch[current_hartid()]);
	opteed_exit_sp(optee_ctx->c_rt_ctx, ret);

	/* Should never reach here */
	assert(0);
}

int32_t opteed_cpu_off_handler(uint32_t linear_id)
{
	return 0;
}

void opteed_cpu_on_handler(uint32_t linear_id)
{
	int32_t rc = 0;
	uint64_t saved_mie;
	optee_context_t *optee_ctx = &opteed_sp_context[linear_id];
	entry_point_info_t img_entry_point;

	assert(optee_vector_table);
	assert(get_optee_pstate(optee_ctx->state) == OPTEE_PSTATE_OFF);

	sbi_memset(&img_entry_point, 0, sizeof(entry_point_info_t));
	img_entry_point.sec_attr = SECURE;
	/* optee os run addr*/
	img_entry_point.pc = (uint64_t)&optee_vector_table->cpu_on_entry;
	img_entry_point.arg0 = linear_id;
	/* Initialise this cpu's secure context */
	cm_init_my_context(&img_entry_point);

	saved_mie = csr_read(CSR_MIE);
	/*
	 * Init optee MIE
	 * disable all interrupt
	 */
	csr_write(CSR_MIE, 0);
	/* Enter OPTEE */
	rc = opteed_synchronous_sp_entry(optee_ctx);
	/*
	 * Read the response from OPTEE. A non-zero return means that
	 * something went wrong while communicating with OPTEE.
	 */
	assert(rc == 0);
	/* restore mie for normal world */
	csr_write(CSR_MIE, saved_mie);

	/* Update its context to reflect the state OPTEE is in */
	set_optee_pstate(optee_ctx->state, OPTEE_PSTATE_ON);
	sbi_memset(&img_entry_point, 0, sizeof(entry_point_info_t));
	img_entry_point.sec_attr = NON_SECURE;
	cm_init_my_context(&img_entry_point);
	cm_set_next_eret_context(NON_SECURE);
}
