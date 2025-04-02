/*
 * Copyright (c) 2013-2014, ARM Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <sbi/riscv_encoding.h>
#include <sbi/sbi_trap.h>
#include <sbi/sbi_scratch.h>

	.global	opteed_enter_sp
	/* ---------------------------------------------
	 * This function is called with SP_EL0 as stack.
	 * Here we stash our EL3 callee-saved registers
	 * on to the stack as a part of saving the C
	 * runtime and enter the secure payload.
	 * 'x0' contains a pointer to the memory where
	 * the address of the C runtime context is to be
	 * saved.
	 * ---------------------------------------------
	 */
opteed_enter_sp:
	/* Make space for the registers that we're going to save */
	REG_S sp, 0(a0)

	/* Save callee-saved registers on to the stack */
	REG_S s0, (12-13) * __SIZEOF_POINTER__(sp)
	REG_S s1, (11-13) * __SIZEOF_POINTER__(sp)
	REG_S s2, (10-13) * __SIZEOF_POINTER__(sp)
	REG_S s3, (9-13) * __SIZEOF_POINTER__(sp)
	REG_S s4, (8-13) * __SIZEOF_POINTER__(sp)
	REG_S s5, (7-13) * __SIZEOF_POINTER__(sp)
	REG_S s6, (6-13) * __SIZEOF_POINTER__(sp)
	REG_S s7, (5-13) * __SIZEOF_POINTER__(sp)
	REG_S s8, (4-13) * __SIZEOF_POINTER__(sp)
	REG_S s9, (3-13) * __SIZEOF_POINTER__(sp)
	REG_S s10,(2-13) * __SIZEOF_POINTER__(sp)
	REG_S s11,(1-13) * __SIZEOF_POINTER__(sp)
	REG_S ra, (0-13) * __SIZEOF_POINTER__(sp)

	addi	sp, sp, -13 * __SIZEOF_POINTER__
	/* ---------------------------------------------
	 * Everything is setup now.
	 * use the secure context to restore to the
	 * general purpose and EL3 system registers to
	 * ERET into OPTEE.
	 * ---------------------------------------------
	 */
	/* use secure context*/
	li a0, 0
	call cm_get_context
	mv s0, a0
	REG_L	a0, SBI_TRAP_REGS_OFFSET(a0)(s0)
	REG_L	a1, SBI_TRAP_REGS_OFFSET(a1)(s0)
	REG_L	a2, SBI_TRAP_REGS_OFFSET(a2)(s0)
	REG_L	a3, SBI_TRAP_REGS_OFFSET(a3)(s0)
	REG_L	a4, SBI_TRAP_REGS_OFFSET(a4)(s0)
	REG_L	a5, SBI_TRAP_REGS_OFFSET(a5)(s0)
	REG_L	a6, SBI_TRAP_REGS_OFFSET(a6)(s0)
	REG_L	a7, SBI_TRAP_REGS_OFFSET(a7)(s0)
	/* Restore MEPC and MSTATUS CSRs */
	REG_L	s1, SBI_TRAP_REGS_OFFSET(mepc)(s0)
	csrw	CSR_MEPC, s1
	REG_L	s1, SBI_TRAP_REGS_OFFSET(mstatus)(s0)
	csrw	CSR_MSTATUS, s1
	mret

	.global opteed_exit_sp
opteed_exit_sp:
	/* Restore the previous stack */
	mv sp, a0
	/* Save callee-saved registers on to the stack */
	REG_L s0, (12-13) * __SIZEOF_POINTER__(sp)
	REG_L s1, (11-13) * __SIZEOF_POINTER__(sp)
	REG_L s2, (10-13) * __SIZEOF_POINTER__(sp)
	REG_L s3, (9-13) * __SIZEOF_POINTER__(sp)
	REG_L s4, (8-13) * __SIZEOF_POINTER__(sp)
	REG_L s5, (7-13) * __SIZEOF_POINTER__(sp)
	REG_L s6, (6-13) * __SIZEOF_POINTER__(sp)
	REG_L s7, (5-13) * __SIZEOF_POINTER__(sp)
	REG_L s8, (4-13) * __SIZEOF_POINTER__(sp)
	REG_L s9, (3-13) * __SIZEOF_POINTER__(sp)
	REG_L s10,(2-13) * __SIZEOF_POINTER__(sp)
	REG_L s11,(1-13) * __SIZEOF_POINTER__(sp)
	REG_L ra, (0-13) * __SIZEOF_POINTER__(sp)

	/* ---------------------------------------------
	 * This should take us back to the instruction
	 * after the call to the last opteed_enter_sp().
	 * Place the second parameter to x0 so that the
	 * caller will see it as a return value from the
	 * original entry call
	 * ---------------------------------------------
	 */
	mv	a0, a1
	ret

	.global cm_restore_context_lowlevel
cm_restore_context_lowlevel:
	REG_L	t0, SBI_TRAP_REGS_OFFSET(mepc)(a0)
	csrw	mepc, t0
	REG_L	t0, SBI_TRAP_REGS_OFFSET(mstatus)(a0)
	csrw	mstatus, t0
	beqz	a1, no_update_mepc
	csrw	mepc, a1
	/*
	 * when need to update mepc,indicate it will
	 * come to S mode, so set MPP to supervisor.
	 */
	li		a1, (1 << 12)
	csrc	mstatus, a1
	li		a1, (1 << 11)
	csrs	mstatus, a1
no_update_mepc:
	REG_L	ra, SBI_TRAP_REGS_OFFSET(ra)(a0)
	REG_L	sp, SBI_TRAP_REGS_OFFSET(sp)(a0)
	REG_L	gp, SBI_TRAP_REGS_OFFSET(gp)(a0)
	REG_L	tp, SBI_TRAP_REGS_OFFSET(tp)(a0)
	REG_L	t0, SBI_TRAP_REGS_OFFSET(t0)(a0)
	REG_L	t1, SBI_TRAP_REGS_OFFSET(t1)(a0)
	REG_L	t2, SBI_TRAP_REGS_OFFSET(t2)(a0)
	REG_L	s0, SBI_TRAP_REGS_OFFSET(s0)(a0)
	REG_L	s1, SBI_TRAP_REGS_OFFSET(s1)(a0)
	REG_L	a1, SBI_TRAP_REGS_OFFSET(a1)(a0)
	REG_L	a2, SBI_TRAP_REGS_OFFSET(a2)(a0)
	REG_L	a3, SBI_TRAP_REGS_OFFSET(a3)(a0)
	REG_L	a4, SBI_TRAP_REGS_OFFSET(a4)(a0)
	REG_L	a5, SBI_TRAP_REGS_OFFSET(a5)(a0)
	REG_L	a6, SBI_TRAP_REGS_OFFSET(a6)(a0)
	REG_L	a7, SBI_TRAP_REGS_OFFSET(a7)(a0)
	REG_L	s2, SBI_TRAP_REGS_OFFSET(s2)(a0)
	REG_L	s3, SBI_TRAP_REGS_OFFSET(s3)(a0)
	REG_L	s4, SBI_TRAP_REGS_OFFSET(s4)(a0)
	REG_L	s5, SBI_TRAP_REGS_OFFSET(s5)(a0)
	REG_L	s6, SBI_TRAP_REGS_OFFSET(s6)(a0)
	REG_L	s7, SBI_TRAP_REGS_OFFSET(s7)(a0)
	REG_L	s8, SBI_TRAP_REGS_OFFSET(s8)(a0)
	REG_L	s9, SBI_TRAP_REGS_OFFSET(s9)(a0)
	REG_L	s10, SBI_TRAP_REGS_OFFSET(s10)(a0)
	REG_L	s11, SBI_TRAP_REGS_OFFSET(s11)(a0)
	REG_L	t3, SBI_TRAP_REGS_OFFSET(t3)(a0)
	REG_L	t4, SBI_TRAP_REGS_OFFSET(t4)(a0)
	REG_L	t5, SBI_TRAP_REGS_OFFSET(t5)(a0)
	REG_L	t6, SBI_TRAP_REGS_OFFSET(t6)(a0)
	/* load a0 register at last */
	REG_L	a0, SBI_TRAP_REGS_OFFSET(a0)(a0)
	mret

#ifdef CFG_WITH_VFP
	.global	vfp_save_extension_regs
vfp_save_extension_regs:
	fsd f0, 0(a0)
	fsd f1, 1*8(a0)
	fsd f2, 2*8(a0)
	fsd f3, 3*8(a0)
	fsd f4, 4*8(a0)
	fsd f5, 5*8(a0)
	fsd f6, 6*8(a0)
	fsd f7, 7*8(a0)
	fsd f8, 8*8(a0)
	fsd f9, 9*8(a0)
	fsd f10, 10*8(a0)
	fsd f11, 11*8(a0)
	fsd f12, 12*8(a0)
	fsd f13, 13*8(a0)
	fsd f14, 14*8(a0)
	fsd f15, 15*8(a0)
	fsd f16, 16*8(a0)
	fsd f17, 17*8(a0)
	fsd f18, 18*8(a0)
	fsd f19, 19*8(a0)
	fsd f20, 20*8(a0)
	fsd f21, 21*8(a0)
	fsd f22, 22*8(a0)
	fsd f23, 23*8(a0)
	fsd f24, 24*8(a0)
	fsd f25, 25*8(a0)
	fsd f26, 26*8(a0)
	fsd f27, 27*8(a0)
	fsd f28, 28*8(a0)
	fsd f29, 29*8(a0)
	fsd f30, 30*8(a0)
	fsd f31, 31*8(a0)
	ret

	.global	vfp_restore_extension_regs
vfp_restore_extension_regs:
	fld f0, 0(a0)
	fld f1, 1*8(a0)
	fld f2, 2*8(a0)
	fld f3, 3*8(a0)
	fld f4, 4*8(a0)
	fld f5, 5*8(a0)
	fld f6, 6*8(a0)
	fld f7, 7*8(a0)
	fld f8, 8*8(a0)
	fld f9, 9*8(a0)
	fld f10, 10*8(a0)
	fld f11, 11*8(a0)
	fld f12, 12*8(a0)
	fld f13, 13*8(a0)
	fld f14, 14*8(a0)
	fld f15, 15*8(a0)
	fld f16, 16*8(a0)
	fld f17, 17*8(a0)
	fld f18, 18*8(a0)
	fld f19, 19*8(a0)
	fld f20, 20*8(a0)
	fld f21, 21*8(a0)
	fld f22, 22*8(a0)
	fld f23, 23*8(a0)
	fld f24, 24*8(a0)
	fld f25, 25*8(a0)
	fld f26, 26*8(a0)
	fld f27, 27*8(a0)
	fld f28, 28*8(a0)
	fld f29, 29*8(a0)
	fld f30, 30*8(a0)
	fld f31, 31*8(a0)
	ret
#endif