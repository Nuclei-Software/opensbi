/*
 * Copyright (c) 2013-2017, ARM Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef OPTEED_PRIVATE_H
#define OPTEED_PRIVATE_H

#include <sbi/sbi_types.h>
#include "context_manage.h"
#include "sm.h"
/*******************************************************************************
 * OPTEE PM state information e.g. OPTEE is suspended, uninitialised etc
 * and macros to access the state information in the per-cpu 'state' flags
 ******************************************************************************/
#define OPTEE_PSTATE_OFF		0
#define OPTEE_PSTATE_ON			1
#define OPTEE_PSTATE_SUSPEND		2
#define OPTEE_PSTATE_SHIFT		0
#define OPTEE_PSTATE_MASK		0x3
#define get_optee_pstate(state)	((state >> OPTEE_PSTATE_SHIFT) & \
				 OPTEE_PSTATE_MASK)
#define clr_optee_pstate(state)	(state &= ~(OPTEE_PSTATE_MASK \
					    << OPTEE_PSTATE_SHIFT))
#define set_optee_pstate(st, pst) do {					       \
					clr_optee_pstate(st);		       \
					st |= (pst & OPTEE_PSTATE_MASK) <<     \
						OPTEE_PSTATE_SHIFT;	       \
				} while (0)

/*******************************************************************************
 * Number of cpus that the present on this platform. TODO: Rely on a topology
 * tree to determine this in the future to avoid assumptions about mpidr
 * allocation
 ******************************************************************************/


typedef uint32_t optee_vector_isn_t;

typedef struct optee_vectors {
	optee_vector_isn_t yield_smc_entry;
	optee_vector_isn_t fast_smc_entry;
	optee_vector_isn_t cpu_on_entry;
	optee_vector_isn_t cpu_off_entry;
	optee_vector_isn_t cpu_resume_entry;
	optee_vector_isn_t cpu_suspend_entry;
	optee_vector_isn_t fiq_entry;
	optee_vector_isn_t system_off_entry;
	optee_vector_isn_t system_reset_entry;
} optee_vectors_t;



/*******************************************************************************
 * Function & Data prototypes
 ******************************************************************************/
int32_t opteed_init(void);
uint64_t opteed_enter_sp(uint64_t *c_rt_ctx);
void __noreturn opteed_exit_sp(uint64_t c_rt_ctx, uint64_t ret);
uint64_t opteed_synchronous_sp_entry(optee_context_t *optee_ctx);
void __noreturn opteed_synchronous_sp_exit(optee_context_t *optee_ctx, uint64_t ret);

extern struct optee_vectors *optee_vector_table;

#endif /* OPTEED_PRIVATE_H */
