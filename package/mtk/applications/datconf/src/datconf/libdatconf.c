// SPDX-License-Identifier:	LGPL-2.1
/*
 * Utility for editing dat files used by MediaTek Wi-Fi drivers
 *
 * Copyright (C) 2019 MediaTek Inc. All Rights Reserved.
 *
 * Author: Weijie Gao <weijie.gao@mediatek.com>
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "libdatconf.h"

/* SSID can contain whitespaces and should not be stripped */
const char *dat_nostrip_list[] = {
	"SSID",
	"SSID1",
	"SSID2",
	"SSID3",
	"SSID4",
	"SSID5",
	"SSID6",
	"SSID7",
	"SSID8",
	"SSID9",
	"SSID10",
	"SSID11",
	"SSID12",
	"SSID13",
	"SSID14",
	"SSID15",
	"SSID16",
	"ApCliSsid",
	NULL
};

const char *get_dat_path_by_index(uint32_t index)
{
	struct kvc_context *ctx;
	char key[40];
	const char *path;

	ctx = kvc_load(L1PROFILE_PATH, LF_STRIP_WHITESPACE);
	if (!ctx)
		return NULL;

	snprintf(key, sizeof(key), "INDEX%u_profile_path", index);
	key[sizeof(key) - 1] = 0;

	path = kvc_get(ctx, key);
	if (path)
		path = strdup(path);

	kvc_unload(ctx);

	return path;
}

const char *get_dat_path_by_name(const char *name)
{
	struct kvc_context *ctx;
	struct kvc_enum_context *ectx;
	const union conf_item *item;
	char key[40], *end;
	const char *path = NULL;
	uint32_t index;

	ctx = kvc_load(L1PROFILE_PATH, LF_STRIP_WHITESPACE);
	if (!ctx)
		return NULL;

	ectx = kvc_enum_init(ctx);

	while (!kvc_enum_next(ectx, &item)) {
		if (strncmp(item->key, "INDEX", 5))
			continue;

		if (!isdigit(item->key[5]))
			continue;

		index = strtoul(&item->key[5], &end, 10);
		if (*end)
			continue;

		if (strcmp(item->value, name))
			continue;

		snprintf(key, sizeof(key), "INDEX%u_profile_path", index);
		key[sizeof(key) - 1] = 0;

		path = kvc_get(ctx, key);
		if (path) {
			path = strdup(path);
			break;
		}
	}

	kvc_enum_end(ectx);

	kvc_unload(ctx);

	return path;
}

const char *get_dat_path_by_ord(uint32_t ord)
{
	struct kvc_context *ctx;
	struct kvc_enum_context *ectx;
	const union conf_item *item;
	char *kp, *vd;
	const char *path = NULL;
	uint32_t curidx = 0, semcnt;

	ctx = kvc_load(L1PROFILE_PATH, LF_STRIP_WHITESPACE);
	if (!ctx)
		return NULL;

	ectx = kvc_enum_init(ctx);

	while (!kvc_enum_next(ectx, &item)) {
		if (strncmp(item->key, "INDEX", 5))
			continue;

		kp = &item->key[5];

		while (isdigit(*kp))
			kp++;

		if (kp == &item->key[5])
			continue;

		if (strcmp(kp, "_profile_path"))
			continue;

		vd = strchr(item->value, ';');

		if (!vd) {
			if (curidx < ord) {
				curidx++;
				continue;
			}

			path = strdup(item->value);
			break;
		} else {
			semcnt = 0;

			while (vd) {
				semcnt++;
				vd = strchr(vd + 1, ';');
			}

			if (ord - curidx > semcnt) {
				curidx += semcnt + 1;
				continue;
			}

			path = dat_get_indexed_value(item->value, ord - curidx);
			break;
		}
	}

	kvc_enum_end(ectx);

	kvc_unload(ctx);

	return path;
}

void free_dat_path(const char *path)
{
	if (path)
		free((void *) path);
}

struct kvc_context *dat_load(const char *file)
{
	return kvc_load_opt(file, DATCONF_LF_FLAGS, dat_nostrip_list);
}

struct kvc_context *dat_load_by_index(uint32_t index)
{
	const char *path;
	struct kvc_context *ctx;

	path = get_dat_path_by_index(index);
	if (!path)
		return NULL;

	ctx = kvc_load_opt(path, DATCONF_LF_FLAGS, dat_nostrip_list);

	free_dat_path(path);

	return ctx;
}

struct kvc_context *dat_load_by_name(const char *name)
{
	const char *path;
	struct kvc_context *ctx;

	path = get_dat_path_by_name(name);
	if (!path)
		return NULL;

	ctx = kvc_load_opt(path, DATCONF_LF_FLAGS, dat_nostrip_list);

	free_dat_path(path);

	return ctx;
}

struct kvc_context *dat_load_raw(const char *str, size_t len)
{
	return kvc_load_buf_opt(str, len, DATCONF_LF_FLAGS, dat_nostrip_list);
}

const char *dat_get_indexed_value(const char *str, size_t idx)
{
	const char *ph, *pt, *p;
	size_t n = 0, len;
	char *val;

	/* points to start of a field */
	ph = str;

	while (ph) {
		/* points to end of the field (the next char) */
		pt = strchr(ph, ';');

		if (n == idx) {
			if (!pt)
				/*
				 * end of the string, let it point to the NULL
				 * char
				 */
				p = ph + strlen(ph);
			else
				p = pt;

			len = p - ph;

			val = malloc(len + 1);
			if (!val)
				return NULL;

			memcpy(val, ph, len);
			val[len] = 0;

			return val;
		}

		if (!pt)
			break;

		ph = pt + 1;
		n++;
	}

	return NULL;
}

const char *dat_set_indexed_value(const char *str, size_t idx, const char *val)
{
	const char *ph, *pt, *p;
	size_t n = 0, len, lens, lenv, lend, pos;
	char *s;

	if (!val)
		val = "";

	ph = str;

	while (ph) {
		pt = strchr(ph, ';');

		if (n == idx) {
			/* replace an existing field */

			if (!pt)
				p = ph + strlen(ph);
			else
				p = pt;

			lenv = strlen(val);
			lens = strlen(str);
			lend = p - ph;
			len = lens - lend + lenv;
			pos = ph - str;

			s = malloc(len + 1);
			if (!s)
				return NULL;

			memcpy(s, str, pos);
			memcpy(s + pos, val, lenv);
			memcpy(s + pos + lenv, ph + lend, lens - pos - lend);
			s[len] = 0;

			return s;
		}

		if (!pt)
			break;

		ph = pt + 1;
		n++;
	}

	/* new field */

	lens = strlen(str);
	lenv = strlen(val);
	pos = idx - n;
	len = lens + pos + lenv;

	s = malloc(len + 1);
	if (!s)
		return NULL;

	memcpy(s, str, lens);
	memset(s + lens, ';', pos);
	memcpy(s + lens + pos, val, lenv);
	s[len] = 0;

	return s;
}
