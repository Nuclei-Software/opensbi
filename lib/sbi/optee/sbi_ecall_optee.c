/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2022 Nuclei System Technology.
 * 
 */

#include <sbi/sbi_ecall.h>
#include <sbi/sbi_ecall_interface.h>
#include <sbi/sbi_error.h>
#include <sbi/sbi_platform.h>
#include <sbi/sbi_trap.h>
#include "context_manage.h"
#include "opteed_private.h"
#include "teesmc_opteed.h"
#include "optee_smc.h"
#include "teesmc_opteed_macros.h"

static int is_caller_non_secure(void)
{
	cpu_context_t *ctx;

	ctx = cm_get_next_context();

	return (ctx->sec_attr == NON_SECURE);
}

extern u32 optee_saved_sie[PLATFORM_CORE_COUNT];
extern u32 optee_saved_mstatus_sie[PLATFORM_CORE_COUNT];

u32 optee_saved_csr_mie[PLATFORM_CORE_COUNT] = {0};

static int sbi_ecall_optee_handler(unsigned long extid, unsigned long funcid,
				    const struct sbi_trap_regs *regs,
				    unsigned long *out_val,
				    struct sbi_trap_info *out_trap)
{
	cpu_context_t *cpu_context;
	uint32_t linear_id = current_hartid();
	optee_context_t *optee_ctx = &opteed_sp_context[linear_id];

	/*
	 * Determine which security state this SMC originated from
	 */
	if (is_caller_non_secure()) {
		/*
		 * when linux send fastcall, M mode interrupt should be saved and disabled,
		 * when tee finish the fastcall, M mode interrupt should be restore.
		 */
		optee_saved_csr_mie[linear_id] = csr_read(CSR_MIE);
		/*
		 * This is a fresh request from the non-secure client.
		 * Save the non-secure state and
		 * send the request to the secure payload.
		 */
		cm_gpregs_context_save(NON_SECURE, regs);
		cm_sysregs_context_save(NON_SECURE);
		cm_vfp_context_save(NON_SECURE);
		/*
		 * We are done stashing the non-secure context. Ask the
		 * OPTEE to do the work now.
		 */
		assert(&optee_ctx->cpu_ctx == cm_get_context(SECURE));

		optee_ctx->cpu_ctx.gp_regs.a0 = regs->a0;
		optee_ctx->cpu_ctx.gp_regs.a1 = regs->a1;
		optee_ctx->cpu_ctx.gp_regs.a2 = regs->a2;
		optee_ctx->cpu_ctx.gp_regs.a3 = regs->a3;
		optee_ctx->cpu_ctx.gp_regs.a4 = regs->a4;
		optee_ctx->cpu_ctx.gp_regs.a5 = regs->a5;
		optee_ctx->cpu_ctx.gp_regs.a6 = regs->a6;
		optee_ctx->cpu_ctx.gp_regs.a7 = regs->a7;

		if (GET_SMC_TYPE(funcid) == SMC_TYPE_FAST) {
			cm_set_mepc(SECURE, (uint64_t)
					&optee_vector_table->fast_smc_entry);
		} else {
			cm_set_mepc(SECURE, (uint64_t)
					&optee_vector_table->yield_smc_entry);
		}

		cm_set_next_eret_context(SECURE);
		/*
		 * Disable M mode interrupt before enter into secure world,
		 * secure world will not be interrupted by foreign interrupt
		 */
		csr_clear(CSR_MIE, MIP_MEIP);
		csr_clear(CSR_MIE, MIP_MTIP);
		csr_clear(CSR_MIE, MIP_MSIP);
		cm_restore_next_context(SECURE, 0);
	}
	/*
	 * Returning from OPTEE
	 */
	switch (funcid) {
	/*
	 * OPTEE has finished initialising itself after a cold boot
	 */
	case TEESMC_OPTEED_RETURN_ENTRY_DONE:
		/*
		 * Stash the OPTEE entry points information. This is done
		 * only once on the primary cpu
		 */
		cm_gpregs_context_save(SECURE, regs);
		cm_sysregs_context_save(SECURE);
		assert(optee_vector_table == NULL);
		optee_vector_table = (optee_vectors_t *) regs->a1;

		if (optee_vector_table) {
			set_optee_pstate(optee_ctx->state, OPTEE_PSTATE_ON);
		}
		/*
		 * OPTEE reports completion. The OPTEED must have initiated
		 * the original request through a synchronous entry into
		 * OPTEE. Jump back to the original C runtime context.
		 */
		opteed_synchronous_sp_exit(optee_ctx, regs->a1);
		break;


	/*
	 * These function IDs is used only by OP-TEE to indicate it has
	 * finished:
	 * 1. turning itself on in response to an earlier psci
	 *    cpu_on request
	 * 2. resuming itself after an earlier psci cpu_suspend
	 *    request.
	 */
	case TEESMC_OPTEED_RETURN_ON_DONE:
	case TEESMC_OPTEED_RETURN_RESUME_DONE:
	/*
	 * These function IDs is used only by the SP to indicate it has
	 * finished:
	 * 1. suspending itself after an earlier psci cpu_suspend
	 *    request.
	 * 2. turning itself off in response to an earlier psci
	 *    cpu_off request.
	 */
	case TEESMC_OPTEED_RETURN_OFF_DONE:
	case TEESMC_OPTEED_RETURN_SUSPEND_DONE:
	case TEESMC_OPTEED_RETURN_SYSTEM_OFF_DONE:
	case TEESMC_OPTEED_RETURN_SYSTEM_RESET_DONE:
		cm_gpregs_context_save(SECURE, regs);
		cm_sysregs_context_save(SECURE);
		/*
		 * OPTEE reports completion. The OPTEED must have initiated the
		 * original request through a synchronous entry into OPTEE.
		 * Jump back to the original C runtime context, and pass x1 as
		 * return value to the caller
		 */
		opteed_synchronous_sp_exit(optee_ctx, regs->a1);
		break;

	/*
	 * OPTEE is returning from a call or being preempted from a call, in
	 * either case execution should resume in the normal world.
	 */
	case TEESMC_OPTEED_RETURN_CALL_DONE:
		/*
		 * This is the result from the secure client of an
		 * earlier request. The results are in x0-x3. Copy it
		 * into the non-secure context, save the secure state
		 * and return to the non-secure state.
		 */
		cm_gpregs_context_save(SECURE, regs);
		cm_sysregs_context_save(SECURE);
		cm_vfp_context_save(SECURE);
		/* Get a reference to the non-secure context */
		cpu_context = cm_get_context(NON_SECURE);
		assert(cpu_context);

		cpu_context->gp_regs.a0 = regs->a1;
		cpu_context->gp_regs.a1 = regs->a2;
		cpu_context->gp_regs.a2 = regs->a3;
		cpu_context->gp_regs.a3 = regs->a4;
		/* skip ecall*/
		cpu_context->gp_regs.mepc +=4;

		cm_set_next_eret_context(NON_SECURE);
		csr_write(CSR_MIE, optee_saved_csr_mie[linear_id]);
		csr_set(CSR_MIE, MIP_MEIP);
		/* Restore non-secure state */
		cm_restore_next_context(NON_SECURE, 0);
		/*never come to here!*/
		break;
	/*
	 * OPTEE has finished handling a S-EL1 FIQ interrupt. Execution
	 * should resume in the normal world.
	 */
	case TEESMC_OPTEED_RETURN_FIQ_DONE:
		/* After forward FIQ, enable M mode timer/plic interrupt*/
		cpu_context = cm_get_context(SECURE);
		assert(cpu_context);
		cpu_context->s_csrs.sie = optee_saved_sie[linear_id];
		cpu_context->gp_regs.mstatus |= optee_saved_mstatus_sie[linear_id] & MSTATUS_SIE;
		/*
		 * Restore non-secure state. There is no need to save the
		 * secure system register context since OPTEE was supposed
		 * to preserve it during S-EL1 interrupt handling.
		 */
		cm_set_next_eret_context(NON_SECURE);
		csr_write(CSR_MIE, optee_saved_csr_mie[linear_id]);
		csr_set(CSR_MIE, MIP_MEIP);
		cm_restore_next_context(NON_SECURE, 0);
		/* never come to here! */
		break;
	default:
		sbi_printf("unkown funid!,%lx\n",funcid);
		sbi_hart_hang();
	}

	return 0;
}

struct sbi_ecall_extension ecall_optee = {
	.extid_start = SBI_EXT_OPTEE,
	.extid_end = SBI_EXT_OPTEE,
	.handle = sbi_ecall_optee_handler,
};
