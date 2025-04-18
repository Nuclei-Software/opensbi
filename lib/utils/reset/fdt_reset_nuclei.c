/*
* SPDX-License-Identifier: BSD-2-Clause
*
*/

#include <libfdt.h>
#include <sbi/riscv_io.h>
#include <sbi/sbi_bitops.h>
#include <sbi/sbi_hart.h>
#include <sbi/sbi_scratch.h>
#include <sbi/sbi_system.h>
#include <sbi_utils/fdt/fdt_helper.h>
#include <sbi_utils/reset/fdt_reset.h>
#include <sbi/sbi_error.h>
#include <sbi/sbi_bitops.h>

#define NUCLEI_RST_MAGIC		0x80000a5f

static uint64_t msftrst_addr;

static int nuclei_system_reset_check(u32 type, u32 reason)
{
	return 1;
}

static void nuclei_system_reset(u32 type, u32 reason)
{
	*(uint32_t volatile*)(size_t)msftrst_addr = NUCLEI_RST_MAGIC;

	while(*(uint32_t volatile*)(size_t)msftrst_addr & BIT(31));
}

static struct sbi_system_reset_device nuclei_reset = {
	.name = "nuclei_reset",
	.system_reset_check = nuclei_system_reset_check,
	.system_reset = nuclei_system_reset
};

static int nuclei_reset_init(void *fdt, int nodeoff,
				const struct fdt_match *match)
{
	int rc;

	rc = fdt_get_node_addr_size(fdt, nodeoff, 0, &msftrst_addr, NULL);
	if (rc < 0)
		return SBI_ENODEV;

	sbi_system_reset_add_device(&nuclei_reset);

	return 0;
}

static const struct fdt_match nuclei_reset_match[] = {
	{ .compatible = "nuclei,sysrst" },
	{ },
};

struct fdt_reset fdt_reset_nuclei = {
	.match_table = nuclei_reset_match,
	.init = nuclei_reset_init
};
