/* Copyright (c) 2012-2014, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <debug.h>
#include <board.h>
#include <smem.h>
#include <baseband.h>
#include <arch/arm.h>
#include <dev_tree.h>
#include <libfdt.h>

static struct board_data board = {UNKNOWN,
	0,
	HW_PLATFORM_UNKNOWN,
	HW_PLATFORM_SUBTYPE_UNKNOWN,
	LINUX_MACHTYPE_UNKNOWN,
	BASEBAND_MSM,
	{{PMIC_IS_INVALID, 0}, {PMIC_IS_INVALID, 0}, {PMIC_IS_INVALID, 0}},
};

static struct fdt_info old_fdt_info = {{},0,0,0,};
static bool has_old_fdt_info = 0;

static void platform_detect()
{
	struct smem_board_info_v6 board_info_v6;
	struct smem_board_info_v7 board_info_v7;
	struct smem_board_info_v8 board_info_v8;
	unsigned int board_info_len = 0;
	unsigned ret = 0;
	unsigned format = 0;
	uint8_t i;
	uint16_t format_major = 0;
	uint16_t format_minor = 0;

	ret = smem_read_alloc_entry_offset(SMEM_BOARD_INFO_LOCATION,
						   &format, sizeof(format), 0);
	if (ret)
		return;

	/* Extract the major & minor version info,
	 * Upper two bytes: major info
	 * Lower two byets: minor info
	 */
	format_major = (format & 0xffff0000) >> 16;
	format_minor = format & 0x0000ffff;

	if (format_major == 0x0)
	{
		if (format_minor == 6)
		{
			board_info_len = sizeof(board_info_v6);

			ret = smem_read_alloc_entry(SMEM_BOARD_INFO_LOCATION,
					&board_info_v6,
					board_info_len);
			if (ret)
				return;

			board.platform = board_info_v6.board_info_v3.msm_id;
			board.platform_version = board_info_v6.board_info_v3.msm_version;
			board.platform_hw = board_info_v6.board_info_v3.hw_platform;
			board.platform_subtype = board_info_v6.platform_subtype;
		}
		else if (format_minor == 7)
		{
			board_info_len = sizeof(board_info_v7);

			ret = smem_read_alloc_entry(SMEM_BOARD_INFO_LOCATION,
					&board_info_v7,
					board_info_len);
			if (ret)
				return;

			board.platform = board_info_v7.board_info_v3.msm_id;
			board.platform_version = board_info_v7.board_info_v3.msm_version;
			board.platform_hw = board_info_v7.board_info_v3.hw_platform;
			board.platform_subtype = board_info_v7.platform_subtype;
			board.pmic_info[0].pmic_type = board_info_v7.pmic_type;
			board.pmic_info[0].pmic_version = board_info_v7.pmic_version;
		}
		else if (format_minor >= 8)
		{
			dprintf(INFO, "Minor socinfo format detected: %u.%u\n", format_major, format_minor);

			board_info_len = sizeof(board_info_v8);

			ret = smem_read_alloc_entry(SMEM_BOARD_INFO_LOCATION,
					&board_info_v8,
					board_info_len);
			if (ret)
				return;

			board.platform = board_info_v8.board_info_v3.msm_id;
			board.platform_version = board_info_v8.board_info_v3.msm_version;
			board.platform_hw = board_info_v8.board_info_v3.hw_platform;
			board.platform_subtype = board_info_v8.platform_subtype;

			/*
			 * fill in board.target with variant_id information
			 * bit no         |31  24 | 23   16            | 15   8             |7         0|
			 * board.target = |subtype| plat_hw_ver major  | plat_hw_ver minor  |hw_platform|
			 *
			 */
			board.target = (((board_info_v8.platform_subtype & 0xff) << 24) |
						   (((board_info_v8.platform_version >> 16) & 0xff) << 16) |
						   ((board_info_v8.platform_version & 0xff) << 8) |
						   (board_info_v8.board_info_v3.hw_platform & 0xff));

			for (i = 0; i < SMEM_V8_SMEM_MAX_PMIC_DEVICES; i++) {
				board.pmic_info[i].pmic_type = board_info_v8.pmic_info[i].pmic_type;
				board.pmic_info[i].pmic_version = board_info_v8.pmic_info[i].pmic_version;
			}
		}
	}
	else
	{
		dprintf(CRITICAL, "Unsupported board info format %u.%u\n", format_major, format_minor);
		ASSERT(0);
	}
}

#ifdef BOOT_2NDSTAGE && DEVICE_TREE
struct dt_entry_v1
{
	uint32_t platform_id;
	uint32_t variant_id;
	uint32_t soc_rev;
	uint32_t offset;
	uint32_t size;
};
static int devtree_parse(void *fdt) {
	int ret = 0;
	int len = 0;
	uint32_t offset = 0;
	const char* str_prop = NULL;
	int min_plat_id_len = DT_ENTRY_V1_SIZE;

	/* Check the device tree header */
	ret = fdt_check_header(fdt);
	if (ret)
	{
		dprintf(CRITICAL, "Invalid device tree header \n");
		return ret;
	}

	/* Get offset of the chosen node */
	ret = fdt_path_offset(fdt, "/chosen");
	if (ret < 0)
	{
		dprintf(CRITICAL, "Could not find chosen node.\n");
		return ret;
	}
	offset = ret;

	/* store cmdline */
	str_prop = (const char *)fdt_getprop(fdt, offset, "bootargs", &len);
	if (str_prop && len>0)
	{
		memcpy(old_fdt_info.cmdline, str_prop, len);
	}

	/* Get offset of the root node */
	ret = fdt_path_offset(fdt, "/");
	if (ret < 0) {
		dprintf(CRITICAL, "Could not find root node.\n");
		return ret;
	}
	offset = ret;

	/* Find the board-id prop from DTB , if board-id is present then
	 * the DTB is version 2 */
	str_prop = (const char *)fdt_getprop(fdt, offset, "qcom,board-id", &len);
	if (str_prop)
	{
		dprintf(CRITICAL, "DTB version 2 is not supported!\n");
		return -1;
	}

	/* Get the msm-id prop from DTB */
	str_prop = (const char *)fdt_getprop(fdt, offset, "qcom,msm-id", &len);
	if (!str_prop || len <= 0) {
		dprintf(CRITICAL, "qcom,msm-id entry not found\n");
		return -1;
	}
	else if (len % min_plat_id_len) {
		dprintf(CRITICAL, "qcom,msm-id in device tree is (%d) not a multiple of (%d)\n",
			len, min_plat_id_len);
		return -1;
	}

	/* store id's */
	old_fdt_info.platform_id = fdt32_to_cpu(((const struct dt_entry_v1 *)str_prop)->platform_id);
	old_fdt_info.variant_id = fdt32_to_cpu(((const struct dt_entry_v1 *)str_prop)->variant_id);
	old_fdt_info.soc_rev = fdt32_to_cpu(((const struct dt_entry_v1 *)str_prop)->soc_rev);

	has_old_fdt_info = 1;
	return 0;
}

struct fdt_info* board_get_old_fdt_info(void) {
	if(has_old_fdt_info)
		return &old_fdt_info;
	else
		return NULL;
}
#endif

void board_init()
{
	platform_detect();
	target_detect(&board);
	target_baseband_detect(&board);

#if BOOT_2NDSTAGE && DEVICE_TREE
	devtree_parse(atags_address);
	dprintf(INFO, "socinfo: platform=%u variant=0x%x soc_rev=0x%x\n", old_fdt_info.platform_id, old_fdt_info.variant_id, old_fdt_info.soc_rev);
#endif
}

uint32_t board_platform_id(void)
{
	return board.platform;
}

uint32_t board_target_id()
{
	return board.target;
}

uint32_t board_baseband()
{
	return board.baseband;
}

uint32_t board_hardware_id()
{
	return board.platform_hw;
}

uint32_t board_hardware_subtype(void)
{
	return board.platform_subtype;
}

uint8_t board_pmic_info(struct board_pmic_data *info, uint8_t num_ent)
{
	uint8_t i;

	for (i = 0; i < num_ent && i < SMEM_MAX_PMIC_DEVICES; i++) {
		info->pmic_type = board.pmic_info[i].pmic_type;
		info->pmic_version = board.pmic_info[i].pmic_version;
		info++;
	}

	return (i--);
}

uint32_t board_soc_version()
{
	return board.platform_version;
}
