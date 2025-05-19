//******************************************************************************
// Copyright (c) 2018, The Regents of the University of California (Regents).
// All Rights Reserved. See LICENSE for license details.
//------------------------------------------------------------------------------
#ifndef _SM_H_
#define _SM_H_

#include <stdint.h>
#include <sbi/riscv_encoding.h>
#include <sbi/sbi_trap.h>
#include "pmp.h"
#include "assert.h"

#ifdef FW_TEXT_START
#define SMM_BASE  FW_TEXT_START
#else
#define SMM_BASE  0x41000000
#endif
#define SMM_SIZE  0x20000

#ifdef FW_OPTEE_SHMEM_BASE
#define OPTEE_SHMEM_BASE		FW_OPTEE_SHMEM_BASE
#else
#define OPTEE_SHMEM_BASE		0x41200000
#endif

#ifdef FW_OPTEE_SHMEM_SIZE
#define OPTEE_SHMEM_SIZE		FW_OPTEE_SHMEM_SIZE
#else
#define OPTEE_SHMEM_SIZE		0x00200000
#endif

#ifdef FW_OPTEE_TZDRAM_BASE
#define OPTEE_TZDRAM_BASE		FW_OPTEE_TZDRAM_BASE
#else
#define OPTEE_TZDRAM_BASE		0x41800000
#endif

#ifdef FW_OPTEE_TZDRAM_SIZE
#define OPTEE_TZDRAM_SIZE		FW_OPTEE_TZDRAM_SIZE
#else
#define OPTEE_TZDRAM_SIZE		0x00800000
#endif

#ifdef FW_OPTEE_PLIC_SIZE
#define OPTEE_PLIC_SIZE			FW_OPTEE_PLIC_SIZE
#else
#define OPTEE_PLIC_SIZE			0x4000000
#endif

#define OPTEE_MAILBOX_BASE		0x97000000
#define OPTEE_MAILBOX_SIZE		0x4000

#ifdef SEC_TIMER_TEST
#define OPTEE_TIMER_BASE		0x10012000
#define OPTEE_TIMER_SIZE		0x1000
#endif

typedef struct entry_point_info {
	uint64_t sec_attr;
	uint64_t pc;
	uint64_t arg0;
	uint64_t arg1;
	uint64_t arg2;
	uint64_t arg3;
	uint64_t arg4;
	uint64_t arg5;
	uint64_t arg6;
	uint64_t arg7;
} entry_point_info_t;

#define SECURE     0
#define NON_SECURE 1

#define PLATFORM_CORE_COUNT		8

#define PMP_UNKNOWN_ERROR                   -1U
#define PMP_SUCCESS                         0
#define PMP_REGION_SIZE_INVALID             20
#define PMP_REGION_NOT_PAGE_GRANULARITY     21
#define PMP_REGION_NOT_ALIGNED              22
#define PMP_REGION_MAX_REACHED              23
#define PMP_REGION_INVALID                  24
#define PMP_REGION_OVERLAP                  25
#define PMP_REGION_IMPOSSIBLE_TOR           26

void sm_init(bool cold_boot);
int teem_pmp_set(uint8_t perm);
int osm_pmp_set(uint8_t perm);
int shm_pmp_set(uint8_t perm);
int plicm_pmp_set(uint8_t perm);
int timerm_pmp_set(uint8_t perm);
int mailboxm_pmp_set(uint8_t perm);

#endif
