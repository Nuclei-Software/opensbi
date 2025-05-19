#ifndef _CONTEXT_MANAGE_H_
#define _CONTEXT_MANAGE_H_

#include <sbi/sbi_trap.h>
#include "sm.h"

struct smode_csrs {
	uintptr_t sstatus;    //Supervisor status register.
	uintptr_t sedeleg;    //Supervisor exception delegation register.
	uintptr_t sideleg;    //Supervisor interrupt delegation register.
	uintptr_t sie;        //Supervisor interrupt-enable register.
	uintptr_t stvec;      //Supervisor trap handler base address.
	uintptr_t scounteren; //Supervisor counter enable

	/*  Supervisor Trap Handling */
	uintptr_t sscratch;   //Scratch register for supervisor trap handlers.
	uintptr_t sepc;       //Supervisor exception program counter.
	uintptr_t scause;     //Supervisor trap cause.
	//NOTE: This should be stval, toolchain issue?
	uintptr_t sbadaddr;   //Supervisor bad address.
	uintptr_t sip;        //Supervisor interrupt pending.

	/*  Supervisor Protection and Translation */
	uintptr_t satp;     //Page-table base register.
};

#ifdef CFG_WITH_VFP
#define VFP_NUM_REGS	32

struct vfp_reg {
	uint64_t v;
};

struct vfp_state{
	struct vfp_reg reg[VFP_NUM_REGS];
	uint32_t status_fs;
	uint32_t fcsr;
};
#endif

typedef struct cpu_context {
	struct sbi_trap_regs gp_regs;
	struct smode_csrs s_csrs;
#ifdef CFG_WITH_VFP
	struct vfp_state vfp_regs;
#endif
	uintptr_t sec_attr;
} cpu_context_t;

/*******************************************************************************
 * Structure which helps the OPTEED to maintain the per-cpu state of OPTEE.
 * 'state'          - collection of flags to track OPTEE state e.g. on/off
 * 'mpidr'          - mpidr to associate a context with a cpu
 * 'c_rt_ctx'       - stack address to restore C runtime context from after
 *                    returning from a synchronous entry into OPTEE.
 * 'cpu_ctx'        - space to maintain OPTEE architectural state
 ******************************************************************************/
typedef struct optee_context {
	uint32_t state;
	uint64_t mpidr;
	uint64_t c_rt_ctx;
	cpu_context_t cpu_ctx;
} optee_context_t;

#define OPTEED_CORE_COUNT		PLATFORM_CORE_COUNT
extern optee_context_t opteed_sp_context[OPTEED_CORE_COUNT];
extern cpu_context_t psci_ns_context[PLATFORM_CORE_COUNT];
extern void *next_cpu_context_ptr[PLATFORM_CORE_COUNT];
/*******************************************************************************
 * Function & variable prototypes
 ******************************************************************************/
void cm_gpregs_context_save(uint32_t security_state,
			    const struct sbi_trap_regs *trap_reg);
void cm_sysregs_context_save(uint32_t security_state);
void cm_sysregs_context_restore(uint32_t security_state);
void *cm_get_context_by_index(unsigned int cpu_idx,
			      unsigned int security_state);
void cm_set_context_by_index(unsigned int cpu_idx,
			     void *context,
			     unsigned int security_state);
void *cm_get_context(uint32_t security_state);
void cm_set_context(void *context, uint32_t security_state);
void cm_init_my_context(const struct entry_point_info *ep);
void cm_init_context_by_index(unsigned int cpu_idx,
			      const struct entry_point_info *ep);
void cm_setup_context(cpu_context_t *ctx, const struct entry_point_info *ep);
void *cm_get_next_context(void);
void cm_set_next_eret_context(uint32_t security_state);
void cm_set_mepc(uint32_t security_state, uintptr_t entrypoint);
void __noreturn cm_restore_next_context(uint32_t security_state, u64 new_epc);
void __noreturn cm_restore_context_lowlevel(cpu_context_t *next, u64 new_epc);

void cm_vfp_context_save(uint32_t security_state);
void cm_vfp_context_restore(uint32_t security_state);

int is_forwarding_interrupt(void);
int is_forwarding_timer_interrupt(void);
int clear_forwarding_interrupt(void);
#endif
