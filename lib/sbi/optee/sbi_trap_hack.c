#include <sbi/riscv_asm.h>
#include <sbi/riscv_encoding.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_ecall.h>
#include <sbi/sbi_error.h>
#include <sbi/sbi_hart.h>
#include <sbi/sbi_illegal_insn.h>
#include <sbi/sbi_ipi.h>
#include <sbi/sbi_misaligned_ldst.h>
#include <sbi/sbi_timer.h>
#include <sbi/sbi_trap.h>
#include "context_manage.h"
#include "optee_smc.h"
#include "opteed_private.h"

typedef enum{
	FORWARD_INT_IDLE=0,
	FORWARD_TIMER_INT_RUNNING,
	FORWARD_PLIC_INT_RUNNING,
} foward_interrupt_t;

static foward_interrupt_t forward_interrupt_flag[PLATFORM_CORE_COUNT];

int is_forwarding_interrupt(void)
{
	return forward_interrupt_flag[current_hartid()] != FORWARD_INT_IDLE;
}

int is_forwarding_timer_interrupt(void)
{
	return forward_interrupt_flag[current_hartid()] == FORWARD_TIMER_INT_RUNNING;
}

int set_forwarding_interrupt(int type)
{
	return forward_interrupt_flag[current_hartid()] = type;
}

int clear_forwarding_interrupt(void)
{
	return forward_interrupt_flag[current_hartid()] = FORWARD_INT_IDLE;
}

static void __noreturn sbi_trap_error(const char *msg, int rc,
				      ulong mcause, ulong mtval, ulong mtval2,
				      ulong mtinst, struct sbi_trap_regs *regs)
{
	u32 hartid = current_hartid();

	sbi_printf("%s: hart%d: %s (error %d)\n", __func__, hartid, msg, rc);
	sbi_printf("%s: hart%d: mcause=0x%" PRILX " mtval=0x%" PRILX "\n",
		   __func__, hartid, mcause, mtval);
	if (misa_extension('H')) {
		sbi_printf("%s: hart%d: mtval2=0x%" PRILX
			   " mtinst=0x%" PRILX "\n",
			   __func__, hartid, mtval2, mtinst);
	}
	sbi_printf("%s: hart%d: mepc=0x%" PRILX " mstatus=0x%" PRILX "\n",
		   __func__, hartid, regs->mepc, regs->mstatus);
	sbi_printf("%s: hart%d: %s=0x%" PRILX " %s=0x%" PRILX "\n", __func__,
		   hartid, "ra", regs->ra, "sp", regs->sp);
	sbi_printf("%s: hart%d: %s=0x%" PRILX " %s=0x%" PRILX "\n", __func__,
		   hartid, "gp", regs->gp, "tp", regs->tp);
	sbi_printf("%s: hart%d: %s=0x%" PRILX " %s=0x%" PRILX "\n", __func__,
		   hartid, "s0", regs->s0, "s1", regs->s1);
	sbi_printf("%s: hart%d: %s=0x%" PRILX " %s=0x%" PRILX "\n", __func__,
		   hartid, "a0", regs->a0, "a1", regs->a1);
	sbi_printf("%s: hart%d: %s=0x%" PRILX " %s=0x%" PRILX "\n", __func__,
		   hartid, "a2", regs->a2, "a3", regs->a3);
	sbi_printf("%s: hart%d: %s=0x%" PRILX " %s=0x%" PRILX "\n", __func__,
		   hartid, "a4", regs->a4, "a5", regs->a5);
	sbi_printf("%s: hart%d: %s=0x%" PRILX " %s=0x%" PRILX "\n", __func__,
		   hartid, "a6", regs->a6, "a7", regs->a7);
	sbi_printf("%s: hart%d: %s=0x%" PRILX " %s=0x%" PRILX "\n", __func__,
		   hartid, "s2", regs->s2, "s3", regs->s3);
	sbi_printf("%s: hart%d: %s=0x%" PRILX " %s=0x%" PRILX "\n", __func__,
		   hartid, "s4", regs->s4, "s5", regs->s5);
	sbi_printf("%s: hart%d: %s=0x%" PRILX " %s=0x%" PRILX "\n", __func__,
		   hartid, "s6", regs->s6, "s7", regs->s7);
	sbi_printf("%s: hart%d: %s=0x%" PRILX " %s=0x%" PRILX "\n", __func__,
		   hartid, "s8", regs->s8, "s9", regs->s9);
	sbi_printf("%s: hart%d: %s=0x%" PRILX " %s=0x%" PRILX "\n", __func__,
		   hartid, "s10", regs->s10, "s11", regs->s11);
	sbi_printf("%s: hart%d: %s=0x%" PRILX " %s=0x%" PRILX "\n", __func__,
		   hartid, "t0", regs->t0, "t1", regs->t1);
	sbi_printf("%s: hart%d: %s=0x%" PRILX " %s=0x%" PRILX "\n", __func__,
		   hartid, "t2", regs->t2, "t3", regs->t3);
	sbi_printf("%s: hart%d: %s=0x%" PRILX " %s=0x%" PRILX "\n", __func__,
		   hartid, "t4", regs->t4, "t5", regs->t5);
	sbi_printf("%s: hart%d: %s=0x%" PRILX "\n", __func__, hartid, "t6",
		   regs->t6);

	sbi_hart_hang();
}

static void clint_mtimer_stop(void)
{
	const struct sbi_timer_device * timer = sbi_timer_get_device();

	if (timer){
		timer->timer_event_stop();
	}
}

extern u32 plic_intr_claim(void);
extern void plic_intr_set_pending(int id);

void forward_int_to_ree(struct sbi_trap_regs *regs, ulong mcause)
{
	if (mcause == IRQ_M_TIMER) {
		/*forward timer interrupt to REE
		 *step1:save tee ctx
		 *step2:set all secure int to M mode,non-secure int to S mode
		 *step3:clear MTIP of MIE,set STIP of MIP,like sbi_timer_process()
		 *step4:construct forward irq request
		 *step5:cm_set_next_eret_context
		 *step6:restore ree ctx
		 */
		cpu_context_t *ns_cpu_context;
		static int cnt = 0;

		/*clear MTIP*/
		clint_mtimer_stop();
		csr_set(CSR_MIP, MIP_STIP);
		if (!((cnt++) & 63))
			sbi_printf("M:fwd timer int to REE\n");
		set_forwarding_interrupt(FORWARD_TIMER_INT_RUNNING);

		cm_gpregs_context_save(SECURE, regs);
		cm_sysregs_context_save(SECURE);
		cm_vfp_context_save(SECURE);
		/* Get a reference to the non-secure context */
		ns_cpu_context = cm_get_context(NON_SECURE);
		assert(ns_cpu_context);
		/* construct forward irq request */
		ns_cpu_context->gp_regs.a0 = OPTEE_SMC_RETURN_RPC_FOREIGN_INTR;
		ns_cpu_context->gp_regs.a1 = 0;
		ns_cpu_context->gp_regs.a2 = 0;
		ns_cpu_context->gp_regs.a3 = 0;
		/* skip ecall*/
		ns_cpu_context->gp_regs.mepc +=4;

		cm_set_next_eret_context(NON_SECURE);
		/* Restore non-secure state */
		cm_restore_next_context(NON_SECURE, 0);
	} else if (mcause == IRQ_M_EXT) {
		cpu_context_t *ns_cpu_context;
		u32 source;

		/*
		 * only claim M mode plic interrupt,
		 * S mode will claim and complete again.
		 */
		source = plic_intr_claim();
		set_forwarding_interrupt(FORWARD_PLIC_INT_RUNNING);

		cm_gpregs_context_save(SECURE, regs);
		cm_sysregs_context_save(SECURE);
		cm_vfp_context_save(SECURE);
		/* Get a reference to the non-secure context */
		ns_cpu_context = cm_get_context(NON_SECURE);
		assert(ns_cpu_context);
		/* construct forward irq request */
		ns_cpu_context->gp_regs.a0 = OPTEE_SMC_RETURN_RPC_FOREIGN_INTR;
		ns_cpu_context->gp_regs.a1 = 0;
		ns_cpu_context->gp_regs.a2 = 0;
		ns_cpu_context->gp_regs.a3 = 0;
		/* skip ecall*/
		ns_cpu_context->gp_regs.mepc +=4;

		cm_set_next_eret_context(NON_SECURE);
		/**
		 * After switch interrupt enable mode, this interrupt enable
		 * mode have been set to S mode.
		 * Manually set interrupt pending to trigger the same interrupt to S mode
		 */
		plic_intr_set_pending(source);
		/* Restore non-secure state */
		cm_restore_next_context(NON_SECURE, 0);
		/*never reach here*/
	}
}

u32 optee_saved_sie[PLATFORM_CORE_COUNT];
u32 optee_saved_mstatus_sie[PLATFORM_CORE_COUNT];
extern u32 optee_saved_csr_mie[PLATFORM_CORE_COUNT];
/**
 * @brief
 * when CPU running on REE, TEE interrupt occur,
 * forward plic interrupt to TEE.
 *
 * @param regs
 */
void forward_int_to_tee(struct sbi_trap_regs *regs)
{
	u32 source;
	cpu_context_t *cpu_context;
	u32 linear_id = current_hartid();

	optee_saved_csr_mie[linear_id] = csr_read(CSR_MIE);
	/**
	 * only claim M mode plic interrupt to clear interrupt pending,
	 * not to complete,so that next interrupt request is blocked.
	 */
	source = plic_intr_claim();
	sbi_printf("M:fwd int%d to TEE\n", source);
	cm_gpregs_context_save(NON_SECURE, regs);
	cm_sysregs_context_save(NON_SECURE);
	cm_vfp_context_save(NON_SECURE);

	cpu_context = cm_get_context(SECURE);
	assert(cpu_context);

	/**
	 * disable M mode plic interrupt and timer interrupt,
	 * to avoid interrupt premption which maybe enter into deadlock.
	 */
	csr_clear(CSR_MIE,  MIP_MTIP);
	csr_clear(CSR_MIE,  MIP_MEIP);
	csr_clear(CSR_MIE,  MIP_MSIP);

	optee_saved_mstatus_sie[current_hartid()] = cpu_context->gp_regs.mstatus;
	/*enable mstatus.sie*/
	cpu_context->gp_regs.mstatus |= MSTATUS_SIE;
	/*save sie*/
	optee_saved_sie[current_hartid()] = cpu_context->s_csrs.sie;
	/*enable sie plic interrupt only*/
	cpu_context->s_csrs.sie = 1 << 9;

	cm_set_next_eret_context(SECURE);
	/**
	 * After switch interrupt enable mode, this interrupt enable
	 * mode have been set to S mode.
	 * Manually set interrupt pending to trigger the same interrupt to S mode
	 */
	plic_intr_set_pending(source);
	/* Restore non-secure state */
	cm_restore_next_context(SECURE, (u64)(&optee_vector_table->fiq_entry));
	/* never reach here */
}
/**
 * Handle trap/interrupt
 *
 * This function is called by firmware linked to OpenSBI
 * library for handling trap/interrupt. It expects the
 * following:
 * 1. The 'mscratch' CSR is pointing to sbi_scratch of current HART
 * 2. The 'mcause' CSR is having exception/interrupt cause
 * 3. The 'mtval' CSR is having additional trap information
 * 4. The 'mtval2' CSR is having additional trap information
 * 5. The 'mtinst' CSR is having decoded trap instruction
 * 6. Stack pointer (SP) is setup for current HART
 * 7. Interrupts are disabled in MSTATUS CSR
 *
 * @param regs pointer to register state
 */
void sbi_trap_handler_sec(struct sbi_trap_regs *regs)
{
	int rc = SBI_ENOTSUPP;
	const char *msg = "trap handler failed";
	ulong mcause = csr_read(CSR_MCAUSE);
	ulong mtval = csr_read(CSR_MTVAL), mtval2 = 0, mtinst = 0;
	struct sbi_trap_info trap;

	if (misa_extension('H')) {
		mtval2 = csr_read(CSR_MTVAL2);
		mtinst = csr_read(CSR_MTINST);
	}

	if (mcause & (1UL << (__riscv_xlen - 1))) {
		mcause &= ~(1UL << (__riscv_xlen - 1));
		switch (mcause) {
		case IRQ_M_TIMER:
			break;
		case IRQ_M_SOFT:
			break;
		case IRQ_M_EXT:
			break;
		default:
			msg = "unknown interrupt";
			break;
			goto trap_error;
		};
		return;
	}

	switch (mcause) {
	case CAUSE_ILLEGAL_INSTRUCTION:
		rc  = sbi_illegal_insn_handler(mtval, regs);
		msg = "illegal instruction handler failed";
		break;
	case CAUSE_MISALIGNED_LOAD:
		rc = sbi_misaligned_load_handler(mtval, mtval2, mtinst, regs);
		msg = "misaligned load handler failed";
		break;
	case CAUSE_MISALIGNED_STORE:
		rc  = sbi_misaligned_store_handler(mtval, mtval2, mtinst, regs);
		msg = "misaligned store handler failed";
		break;
	case CAUSE_SUPERVISOR_ECALL:
	case CAUSE_MACHINE_ECALL:
		rc  = sbi_ecall_handler(regs);
		msg = "ecall handler failed";
		break;
	default:
		/* If the trap came from S or U mode, redirect it there */
		trap.epc = regs->mepc;
		trap.cause = mcause;
		trap.tval = mtval;
		trap.tval2 = mtval2;
		trap.tinst = mtinst;
		rc = sbi_trap_redirect(regs, &trap);
		break;
	};

trap_error:
	if (rc)
		sbi_trap_error(msg, rc, mcause, mtval, mtval2, mtinst, regs);
}
