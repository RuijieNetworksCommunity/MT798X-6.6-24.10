/* SPDX-License-Identifier:	LGPL-2.1 */
/*
 * Private definitons for libkvcutil
 *
 * Copyright (C) 2019 MediaTek Inc. All Rights Reserved.
 *
 * Author: Weijie Gao <weijie.gao@mediatek.com>
 */

#ifndef _KVCUTIL_INTERNAL_H_
#define _KVCUTIL_INTERNAL_H_

#include <stdio.h>
#include "libkvcutil.h"

struct conf_item2 {
	union conf_item item;
	uint32_t deleted: 1;
	uint32_t invalid: 1;
	uint32_t dirty: 1;
	uint32_t nostrip: 1;
	uint8_t eol;
};

struct conf_item_group {
	struct conf_item2 *items;
	size_t used;
	size_t capacity;
};

struct kvc_context {
	FILE *f;
	char *file;
	int flags;

	char *file_data;
	off_t file_size;

	const char **nostrip_list;
	unsigned int dfl_eol;

	struct conf_item_group og;
	struct conf_item_group ng;
};

struct kvc_enum_context {
	struct kvc_context *ctx;
	int first;
	int ng;
	size_t idx;
};

#define NEW_CONF_GROUP_INCREMENT		5

#endif /* _KVCUTIL_INTERNAL_H_ */
