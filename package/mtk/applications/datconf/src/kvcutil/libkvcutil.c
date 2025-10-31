// SPDX-License-Identifier:	LGPL-2.1
/*
 * Utility for editing key-value based config files
 *
 * Copyright (C) 2019 MediaTek Inc. All Rights Reserved.
 *
 * Author: Weijie Gao <weijie.gao@mediatek.com>
 */

#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <malloc.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>

#include "kvcutil-internal.h"

static const char *eol_str[] = {
	[EOL_LF] = "\n",
	[EOL_CR] = "\r",
	[EOL_CRLF] = "\r\n"
};

static const size_t eol_str_len[] = {
	[EOL_LF] = 1,
	[EOL_CR] = 1,
	[EOL_CRLF] = 2
};

/* find the first line-ending char in a string */
static inline char *strcrlf(char *str)
{
	while (*str) {
		if (*str == '\r' || *str == '\n')
			return str;
		str++;
	}

	return NULL;
}

/* find the first NULL char in a string */
static inline char *strnull(char *str, size_t len)
{
	while (len) {
		if (!*str)
			return str;
		str++;
		len--;
	}

	return NULL;
}

/* trim all whitespace chars at the beginning of a string by moving pointer */
static inline char *ltrim(const char *str)
{
	while (*str == 0x20 || *str == '\t')
		str++;

	return (char *) str;
}

/* trim all whitespace chars at the end of a string. chars will be changed */
static inline void rtrim_len(char *str, size_t len)
{
	char *eos = str + len - 1;

	while (eos >= str) {
		if (*eos != 0x20 && *eos != '\t')
			break;

		*eos = 0;
		eos--;
	}
}

/* check if a string is in a NULL-terminated string array */
static bool is_string_in_list(const char *str, const char *list[])
{
	if (!str || !list)
		return false;

	while (*list) {
		if (!strcmp(str, *list))
			return true;

		list++;
	}

	return false;
}

/* parse the raw contents */
static int kvc_parse_content(struct kvc_context *ctx)
{
	char *ptr = ctx->file_data, *pcrlf, *psep, *peol, *pkey;
	uint8_t eol, nostrip;

	if (!ctx->file_size)
		return 0;

	/* at least one line even if the content is empty */
	ctx->og.capacity = 1;

	/*
	 * first step: count total number of lines
	 * this make sure we only allocate memory once
	 */
	do {
		pcrlf = strcrlf(ptr);
		if (!pcrlf)
			break;

		/*
		 * rules of line splitting:
		 *     - CR + LF:  treat as one line (Windows/DOS)
		 *     - CR + ^LF: Mac
		 *     - LF:       Unix/Linux
		 */
		if (pcrlf[0] == '\r' && pcrlf[1] == '\n')
			ptr = pcrlf + 2;
		else
			ptr = pcrlf + 1;

		ctx->og.capacity++;
	} while(1);

	/* 'og' only stores items of the original contents */
	ctx->og.items = calloc(sizeof(*ctx->og.items), ctx->og.capacity);
	if (!ctx->og.items)
		return -ENOMEM;

	/* second step: split lines and parse keys and values */
	ptr = ctx->file_data;

	do {
		peol = strcrlf(ptr);
		pcrlf = peol;

		/* split a line and record its line-ending */
		if (pcrlf) {
			if (pcrlf[0] == '\r' && pcrlf[1] == '\n') {
				pcrlf[0] = 0;
				pcrlf += 2;
				eol = EOL_CRLF;
			} else {
				eol = pcrlf[0] == '\r' ? EOL_CR : EOL_LF;
				pcrlf[0] = 0;
				pcrlf += 1;
			}
		} else {
			/* CR/LF not found. should be the last line */
			peol = strnull(ptr,
				ctx->file_size - (ptr - ctx->file_data) + 1);
			pcrlf = peol + 1;

			/* use default line ending */
			eol = ctx->dfl_eol;
		}

		if (ctx->flags & LF_STRIP_KEY_WHITESPACE)
			pkey = ltrim(ptr);
		else
			pkey = ptr;

		psep = strchr(pkey, '=');

		if (*ltrim(ptr) == '#' || !psep) {
			/* line start with '#' or has no '=' */
			ctx->og.items[ctx->og.used].item.key = ptr;
			ctx->og.items[ctx->og.used].invalid = 1;
			ctx->og.items[ctx->og.used].eol = eol;
			ctx->og.used++;
			goto next_line;
		}

		psep[0] = 0;

		if (ctx->flags & LF_STRIP_KEY_WHITESPACE)
			rtrim_len(pkey, psep - pkey);

		nostrip = 0;

		if (ctx->nostrip_list) {
			/* check if explictly marked as nostrip */
			if (is_string_in_list(pkey, ctx->nostrip_list))
				nostrip = 1;
		}

		if ((ctx->flags & LF_STRIP_VALUE_WHITESPACE) && !nostrip) {
			psep = ltrim(psep + 1);
			rtrim_len(psep, peol - psep);
		} else {
			psep++;
		}

		ctx->og.items[ctx->og.used].item.key = pkey;
		ctx->og.items[ctx->og.used].item.value = psep;
		ctx->og.items[ctx->og.used].eol = eol;
		ctx->og.items[ctx->og.used].nostrip = nostrip;
		ctx->og.used++;

	next_line:
		ptr = pcrlf;
		if (ptr >= ctx->file_data + ctx->file_size)
			break;
	} while(1);

	return 0;
}

/* free string array with its contents */
static void free_nostrip_list(const char *nostrip_list[])
{
	const char **tmp;

	tmp = nostrip_list;
	while (*tmp) {
		free((void *) *tmp);
		tmp++;
	}

	free(nostrip_list);
}

/* duplicate a string array */
static const char **dup_nostrip_list(const char *nostrip_list[])
{
	size_t num = 0, i = 0;
	const char **list, **tmp;

	if (!nostrip_list)
		return NULL;

	/* count number of elements */
	tmp = nostrip_list;
	while (*tmp) {
		num++;
		tmp++;
	}

	/* add one for NULL termination */
	list = calloc(num + 1, sizeof(const char *));
	if (!list)
		return NULL;

	for (i = 0; i < num; i++) {
		list[i] = strdup(nostrip_list[i]);
		if (!list[i])
			goto cleanup;
	}

	return list;

cleanup:
	for (i = 0; i < num; i++) {
		if (list[i])
			free((void *) list[i]);
	}

	free(list);

	return NULL;
}

/* load from file */
struct kvc_context *kvc_load_opt(const char *file, int flags,
				 const char *nostrip_list[])
{
	struct kvc_context *ctx;
	struct stat st;
	int fd, ret;

	ctx = calloc(1, sizeof(*ctx));
	if (!ctx)
		return NULL;

	if (nostrip_list) {
		ctx->nostrip_list = dup_nostrip_list(nostrip_list);
		if (!ctx->nostrip_list)
			goto cleanup3;
	}

	ctx->flags = flags;
	ctx->file = strdup(file);
	if (!ctx->file)
		goto cleanup2;

	/*
	 * open file with both read and write access so that we can
	 * lock the file from beginning
	 */
	fd = open(file, O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	if (fd < 0)
		goto cleanup1;

	if (ctx->flags & LF_FLOCK) {
		/*
		 * use exclusive lock even if we only read this file.
		 * this can make sure RMW (read-modify-write) operation
		 * works correctly.
		 */
		if (flock(fd, LOCK_EX))
			goto cleanup;
	}

	/* get file size through the fd */
	ret = fstat(fd, &st);
	if (ret)
		goto cleanup;

	ctx->file_size = st.st_size;

	/* check if file is too large to be fully loaded into memory */
	if (ctx->file_size >= LONG_MAX)
		goto cleanup;

	/*
	 * direct use of write(2) is very slow.
	 * use stdio to wrap this fd to make use of write cache.
	 */
	ctx->f = fdopen(fd, "r+b");
	if (!ctx->f)
		goto cleanup;

	/* allocate buffer for file contents */
	ctx->file_data = malloc(ctx->file_size + 1);
	if (!ctx->file_data)
		goto cleanup;

	rewind(ctx->f);

	if (fread(ctx->file_data, 1, ctx->file_size, ctx->f) != ctx->file_size)
		goto cleanup;

	ctx->file_data[ctx->file_size] = 0;

	if (kvc_parse_content(ctx))
		goto cleanup;

	return ctx;

cleanup:
	/* unlock the file even if it has not been locked yet */
	if (ctx->flags & LF_FLOCK)
		flock(fd, LOCK_UN);

	/*
	 * if fdopen(3) succeeded, close FILE object only as it will also
	 * close the fd.
	 * otherwise close the fd directly.
	 */
	if (ctx->f)
		fclose(ctx->f);
	else
		close(fd);

	if (ctx->file_data)
		free(ctx->file_data);

	if (ctx->og.items)
		free(ctx->og.items);

cleanup1:
	if (ctx->file)
		free(ctx->file);

cleanup2:
	if (ctx->nostrip_list)
		free_nostrip_list(ctx->nostrip_list);

cleanup3:
	free(ctx);

	return NULL;
}

/* load from file with no nostrip_list */
struct kvc_context *kvc_load(const char *file, int flags)
{
	return kvc_load_opt(file, flags, NULL);
}

/* load from memory buffer */
struct kvc_context *kvc_load_buf_opt(const char *buf, size_t len, int flags,
				     const char *nostrip_list[])
{
	struct kvc_context *ctx;

	ctx = calloc(1, sizeof(*ctx));
	if (!ctx)
		return NULL;

	if (nostrip_list) {
		ctx->nostrip_list = dup_nostrip_list(nostrip_list);
		if (!ctx->nostrip_list)
			goto cleanup;
	}

	ctx->flags = flags;

	ctx->file_size = len;
	ctx->file_data = malloc(len + 1);
	if (!ctx->file_data)
		goto cleanup;

	memcpy(ctx->file_data, buf, len);
	ctx->file_data[len] = 0;

	if (kvc_parse_content(ctx))
		goto cleanup;

	return ctx;

cleanup:
	if (ctx->file_data)
		free(ctx->file_data);

	if (ctx->og.items)
		free(ctx->og.items);

	if (ctx->nostrip_list)
		free_nostrip_list(ctx->nostrip_list);

	free(ctx);

	return NULL;
}

/* load from memory buffer with no nostrip_list */
struct kvc_context *kvc_load_buf(const char *buf, size_t len, int flags)
{
	return kvc_load_buf_opt(buf, len, flags, NULL);
}

/* unload context and release all resources */
int kvc_unload(struct kvc_context *ctx)
{
	size_t i;
	int fd;

	if (!ctx)
		return 0;

	if (ctx->f) {
		/* (optional) unlock the file and close the file */
		if (ctx->flags & LF_FLOCK) {
			fd = fileno(ctx->f);
			if (fd > 0)
				flock(fd, LOCK_UN);
		}

		fclose(ctx->f);
	}

	if (ctx->file)
		free(ctx->file);

	if (ctx->file_data)
		free(ctx->file_data);

	if (ctx->og.items) {
		/* only values marked dirty should be freed */
		for (i = 0; i < ctx->og.used; i++) {
			if (ctx->og.items[i].dirty)
				free(ctx->og.items[i].item.value);
		}

		free(ctx->og.items);
	}

	if (ctx->ng.items) {
		/* all keys and values must be freed */
		for (i = 0; i < ctx->ng.used; i++) {
			free(ctx->ng.items[i].item.key);
			free(ctx->ng.items[i].item.value);
		}

		free(ctx->ng.items);
	}

	if (ctx->nostrip_list)
		free_nostrip_list(ctx->nostrip_list);

	free(ctx);

	return 0;
}

/* write back items to file */
static int write_conf_items(FILE *f, struct kvc_context *ctx,
			    struct conf_item_group *g)
{
	size_t i;
	char *s;
	const char *eol;
	int len, eol_len;

	for (i = 0; i < g->used; i++) {
		if (g->items[i].deleted)
			continue;

		if (g->items[i].invalid) {
			s = ltrim(g->items[i].item.key);

			if (*s == '#' && (ctx->flags & LF_STRIP_COMMENTS))
				continue;
			else if (*s != '#' && (ctx->flags & LF_STRIP_INVALID))
				continue;
		}

		len = strlen(g->items[i].item.key);
		if (fwrite(g->items[i].item.key, 1, len, f) != len)
			return -errno;

		/* write '=' and value only if the line is not marked invalid */
		if (!g->items[i].invalid) {
			if (fwrite("=", 1, 1, f) != 1)
				return -errno;

			len = strlen(g->items[i].item.value);
			if (fwrite(g->items[i].item.value, 1, len, f) != len)
				return -errno;
		}

		/* write line ending char(s) */
		if (ctx->flags & LF_PRESERVE_LINE_ENDING) {
			eol = eol_str[g->items[i].eol];
			eol_len = eol_str_len[g->items[i].eol];
		} else {
			eol = eol_str[ctx->dfl_eol];
			eol_len = eol_str_len[ctx->dfl_eol];
		}

		if (fwrite(eol, 1, eol_len, f) != eol_len)
			return -errno;
	}

	return 0;
}

/* sace changes to file */
int kvc_commit(struct kvc_context *ctx)
{
	int fd, ret = 0;

	if (!ctx)
		return -EINVAL;

	if (!ctx->f)
		return 0;

	/*
	 * stdio has not dedicated function to truncate a file.
	 * ftruncate(2) is the fastest way to do this and fd must be
	 * its first parameter.
	 */
	fd = fileno(ctx->f);
	if (fd < 0)
		return -errno;

	/* move file pinter to the beginning */
	if (fseek(ctx->f, 0, SEEK_SET))
		return -ferror(ctx->f);

	/* truncate all original data */
	if (ftruncate(fd, 0))
		goto cleanup;

	/* write items in 'og' - from original file */
	ret = write_conf_items(ctx->f, ctx, &ctx->og);
	if (ret)
		goto cleanup;

	/* write items in 'ng' - newly created items */
	ret = write_conf_items(ctx->f, ctx, &ctx->ng);

cleanup:
	/* make sure data in cache be written in to storage device */
	fflush(ctx->f);

	/* make sure data for updating configuration file during system poweroff */
	fsync(fd);

	return ret;
}

/* find a key in a group */
static int find_key(struct kvc_context *ctx, struct conf_item_group *g,
		    const char *key, size_t *idx, bool with_deleted)
{
	size_t i;
	int ret;

	for (i = 0; i < g->used; i++) {
		if (g->items[i].invalid)
			continue;

		if (g->items[i].deleted && !with_deleted)
			continue;

		if (ctx->flags & LF_KEY_CASE_SENSITIVE)
			ret = strcmp(key, g->items[i].item.key);
		else
			ret = strcasecmp(key, g->items[i].item.key);

		if (!ret) {
			*idx = i;
			return 0;
		}
	}

	return -1;
}

static int find_key_indexed(struct kvc_context *ctx, struct conf_item_group *g,
			    const char *key, uint32_t ord, size_t *idx,
			    uint32_t *matched_count)
{
	uint32_t n = 0;
	size_t i;
	int ret;

	for (i = 0; i < g->used; i++) {
		if (g->items[i].invalid)
			continue;

		if (g->items[i].deleted)
			continue;

		if (ctx->flags & LF_KEY_CASE_SENSITIVE)
			ret = strcmp(key, g->items[i].item.key);
		else
			ret = strcasecmp(key, g->items[i].item.key);

		if (!ret) {
			if (n == ord) {
				*idx = i;
				return 0;
			}

			n++;
		}
	}

	if (matched_count)
		*matched_count = n;

	return -1;
}

/* count valid items */
size_t kvc_get_count(struct kvc_context *ctx)
{
	size_t n = 0, i;

	if (!ctx)
		return 0;

	for (i = 0; i < ctx->og.used; i++) {
		if (ctx->og.items[i].invalid || ctx->og.items[i].deleted)
			continue;

		n++;
	}

	for (i = 0; i < ctx->ng.used; i++) {
		if (ctx->ng.items[i].deleted)
			continue;

		n++;
	}

	return n;
}

/*
 * strip a given key string.
 * the new string is returned through *new_key.
 * return true means *new_key should freed after use
 */
static bool get_key_stripped(struct kvc_context *ctx, const char *key,
			    const char **new_key)
{
	size_t len;
	const char *keys;
	char *keys2 = NULL;

	if (ctx->flags & LF_STRIP_KEY_WHITESPACE) {
		keys = ltrim(key);
		len = strlen(keys);

		if (keys[len - 1] == '\t' || keys[len - 1] == 0x20) {
			keys2 = strdup(keys);
			rtrim_len(keys2, len);
			keys = keys2;
		}
	} else {
		keys = key;
	}

	*new_key = keys;

	return !!keys2;
}

/* get value specified by a certain key */
const char *kvc_get(struct kvc_context *ctx, const char *key)
{
	const char *keys = NULL, *value = NULL;
	size_t idx = 0;
	bool free_keys;

	if (!ctx || !key)
		return NULL;

	free_keys = get_key_stripped(ctx, key, &keys);

	if (!find_key(ctx, &ctx->og, keys, &idx, false)) {
		value = ctx->og.items[idx].item.value;
		goto cleanup;
	}

	if (!find_key(ctx, &ctx->ng, keys, &idx, false))
		value = ctx->ng.items[idx].item.value;

cleanup:
	if (free_keys)
		free((void *) keys);

	return value;
}

/* get value from a certain key with specified ordinal */
const char *kvc_get_indexed(struct kvc_context *ctx, const char *key,
			    uint32_t index)
{
	const char *keys = NULL, *value = NULL;
	size_t idx = 0;
	bool free_keys;
	uint32_t n;

	if (!ctx || !key)
		return NULL;

	free_keys = get_key_stripped(ctx, key, &keys);

	if (!find_key_indexed(ctx, &ctx->og, keys, index, &idx, &n)) {
		value = ctx->og.items[idx].item.value;
		goto cleanup;
	}

	if (!find_key_indexed(ctx, &ctx->ng, keys, index - n, &idx, NULL))
		value = ctx->ng.items[idx].item.value;

cleanup:
	if (free_keys)
		free((void *)keys);

	return value;
}

/* record all valid items into an array which will be returned */
size_t kvc_get_all(struct kvc_context *ctx, const union conf_item **items)
{
	size_t n, i, idx = 0;
	union conf_item *items_real;

	if (!ctx || !items)
		return 0;

	n = kvc_get_count(ctx);
	if (!n)
		return 0;

	/* no need to zero the memory */
	items_real = malloc(n * sizeof(*items_real));
	if (!items_real)
		return 0;

	for (i = 0; i < ctx->og.used; i++) {
		if (ctx->og.items[i].invalid || ctx->og.items[i].deleted)
			continue;

		items_real[idx] = ctx->og.items[i].item;
		idx++;
	}

	for (i = 0; i < ctx->ng.used; i++) {
		if (ctx->ng.items[i].deleted)
			continue;

		items_real[idx] = ctx->ng.items[i].item;
		idx++;
	}

	*items = items_real;

	return n;
}

/* free the array generated by kvc_get_all */
void kvc_free_items(const union conf_item *items)
{
	if (items)
		free((void *) items);
}

/* increate capacity of 'ng' by <increment> */
static int new_conf_group_extend(struct kvc_context *ctx, size_t increment)
{
	struct conf_item2 *orig;

	/* record the old value */
	orig = ctx->ng.items;

	ctx->ng.items = realloc(ctx->ng.items, (ctx->ng.capacity + increment) *
				sizeof(struct conf_item2));
	if (!ctx->ng.items) {
		/*
		 * realloc returns NULL if failed, but the original pointer
		 * is still valid.
		 */
		ctx->ng.items = orig;
		return -errno;
	}

	memset(ctx->ng.items + ctx->ng.capacity, 0,
	       increment * sizeof(struct conf_item2));
	ctx->ng.capacity += increment;

	return 0;
}

/* get an unused item from 'ng' */
static int get_new_item(struct kvc_context *ctx, size_t *idx)
{
	int ret;

	if (ctx->ng.capacity <= ctx->ng.used) {
		/* extend 'ng' if necessary */
		ret = new_conf_group_extend(ctx, NEW_CONF_GROUP_INCREMENT);
		if (ret)
			return ret;
	}

	*idx = ctx->ng.used++;
	return 0;
}

/* overwrite an existing key */
static int overwrite_key(struct kvc_context *ctx, struct conf_item2 *item,
			 const char *value)
{
	size_t len;
	bool strip;
	char *nv;

	strip = (ctx->flags & LF_STRIP_VALUE_WHITESPACE) && (!item->nostrip);

	item->deleted = 0;

	if (strip)
		value = ltrim(value);

	len = strlen(value);

	/* if old and new values are the same, do nothing */
	if (!strcmp(value, item->item.value))
		return 0;

	if (strlen(item->item.value) >= len) {
		/*
		 * do not re-allocate memory if the current buffer is
		 * long enough to store the new value.
		 */
		memcpy(item->item.value, value, len + 1);
		if (strip)
			rtrim_len(item->item.value, len);
		return 0;
	}

	nv = strdup(value);
	if (!nv)
		return -ENOMEM;

	if (strip)
		rtrim_len(nv, len);

	/* free old value if its marked dirty */
	if (item->dirty)
		free(item->item.value);

	/* always marked dirty if the pointer of value is changed */
	item->item.value = nv;
	item->dirty = 1;

	return 0;
}

/* create an new key in 'ng' */
static int create_key(struct kvc_context *ctx, const char *key,
		      const char *value)
{
	size_t idx = 0;
	int nostrip, ret = 0;
	char *nk = NULL, *nv = NULL;

	nostrip = is_string_in_list(key, ctx->nostrip_list);

	if ((ctx->flags & LF_STRIP_VALUE_WHITESPACE) && !nostrip)
		nv = strdup(ltrim(value));
	else
		nv = strdup(value);

	if (!nv)
		return -errno;

	nk = strdup(key);
	if (!nk) {
		ret = -errno;
		goto cleanup;
	}

	ret = get_new_item(ctx, &idx);
	if (ret)
		goto cleanup;

	if ((ctx->flags & LF_STRIP_VALUE_WHITESPACE) && !nostrip)
		rtrim_len(nv, strlen(nv));

	ctx->ng.items[idx].item.key = nk;
	ctx->ng.items[idx].item.value = nv;
	ctx->ng.items[idx].eol = ctx->dfl_eol;
	ctx->ng.items[idx].nostrip = nostrip;
	ctx->ng.items[idx].dirty = 1;

	return 0;

cleanup:
	if (nv)
		free(nv);

	if (nk)
		free(nk);

	return ret;
}

/* set value for the key specified */
int kvc_set(struct kvc_context *ctx, const char *key, const char *value)
{
	const char *keys = NULL;
	bool free_keys;
	size_t idx = 0;
	int ret = 0;

	if (!ctx)
		return -EINVAL;

	free_keys = get_key_stripped(ctx, key, &keys);

	if (!find_key(ctx, &ctx->og, keys, &idx, true)) {
		ret = overwrite_key(ctx, &ctx->og.items[idx], value);
		goto cleanup;
	}

	if (!find_key(ctx, &ctx->ng, keys, &idx, true)) {
		ret = overwrite_key(ctx, &ctx->ng.items[idx], value);
		goto cleanup;
	}

	ret = create_key(ctx, keys, value);

cleanup:
	if (free_keys)
		free((void *) keys);

	return ret;
}

/* set value for the key specified with ordinal */
int kvc_set_indexed(struct kvc_context *ctx, const char *key, uint32_t index,
		    const char *value)
{
	const char *keys = NULL;
	bool free_keys;
	size_t idx = 0;
	int ret = 0;
	uint32_t n;

	if (!ctx)
		return -EINVAL;

	free_keys = get_key_stripped(ctx, key, &keys);

	if (!find_key_indexed(ctx, &ctx->og, keys, index, &idx, &n)) {
		ret = overwrite_key(ctx, &ctx->og.items[idx], value);
		goto cleanup;
	}

	if (!find_key_indexed(ctx, &ctx->ng, keys, index - n, &idx, NULL)) {
		ret = overwrite_key(ctx, &ctx->ng.items[idx], value);
		goto cleanup;
	}

	ret = -ENOENT;

cleanup:
	if (free_keys)
		free((void *)keys);

	return ret;
}

/* set a list of key-value pairs */
int kvc_set_batch(struct kvc_context *ctx, const union conf_item *items,
		  size_t num)
{
	size_t i = 0;

	if (!ctx || !items || !num)
		return -EINVAL;

	for (i = 0; i < num; i++) {
		if (!items[i].key)
			continue;

		if (items[i].value)
			kvc_set(ctx, items[i].key, items[i].value);
		else
			kvc_set(ctx, items[i].key, "");
	}

	return 0;
}

/* delete an item specified by the key */
int kvc_unset(struct kvc_context *ctx, const char *key)
{
	const char *keys = NULL;
	bool free_keys;
	size_t idx = 0;

	if (!ctx || !key)
		return -EINVAL;

	free_keys = get_key_stripped(ctx, key, &keys);

	if (!find_key(ctx, &ctx->og, keys, &idx, false)) {
		ctx->og.items[idx].deleted = 1;
		goto cleanup;
	}

	if (!find_key(ctx, &ctx->ng, keys, &idx, false))
		ctx->ng.items[idx].deleted = 1;

cleanup:
	if (free_keys)
		free((void *) keys);

	return 0;
}

/* create an enum context */
struct kvc_enum_context *kvc_enum_init(struct kvc_context *ctx)
{
	struct kvc_enum_context *ectx;

	if (!ctx)
		return NULL;

	ectx = calloc(1, sizeof(*ectx));
	if (!ectx)
		return NULL;

	ectx->ctx = ctx;
	ectx->first = 1;

	return ectx;
}

/* return next item of this enum context */
int kvc_enum_next(struct kvc_enum_context *ectx, const union conf_item **item)
{
	struct kvc_context *ctx;
	size_t i, init = 1;

	if (!ectx || !item)
		return -EINVAL;

	ctx = ectx->ctx;

	if (ectx->ng)
		goto find_ng;

	if (ectx->first) {
		ectx->first = 0;
		init = 0;
	}

	for (i = ectx->idx + init; i < ctx->og.used; i++) {
		if (!ctx->og.items[i].deleted && !ctx->og.items[i].invalid) {
			ectx->idx = i;
			*item = &ctx->og.items[i].item;
			return 0;
		}
	}

	ectx->ng = 1;

find_ng:
	for (i = ectx->idx + init; i < ctx->ng.used; i++) {
		if (!ctx->ng.items[i].deleted) {
			ectx->idx = i;
			*item = &ctx->ng.items[i].item;
			return 0;
		}
	}

	return 1;
}

/* free enum context */
int kvc_enum_end(struct kvc_enum_context *ectx)
{
	if (!ectx)
		return -EINVAL;

	free(ectx);

	return 0;
}

/* set default line ending for new pairs */
int kvc_set_default_eol(struct kvc_context *ctx, int eol)
{
	if (!ctx)
		return -EINVAL;

	switch (eol) {
	case EOL_LF:
	case EOL_CR:
	case EOL_CRLF:
		ctx->dfl_eol = eol;
		return 0;
	}

	return -EINVAL;
}
