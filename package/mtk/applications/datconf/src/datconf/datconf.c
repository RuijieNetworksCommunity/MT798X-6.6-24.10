// SPDX-License-Identifier:	LGPL-2.1
/*
 * CLI tool for editing dat files used by MediaTek Wi-Fi drivers
 *
 * Copyright (C) 2019 MediaTek Inc. All Rights Reserved.
 *
 * Author: Weijie Gao <weijie.gao@mediatek.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>

#include "libdatconf.h"

static const char *file;
static int quiet;
static int action;
static char *key;
static char *value;
static int findex = -1;

#define ACTION_LIST		1
#define ACTION_COUNT		2
#define ACTION_GET		3
#define ACTION_SET		4
#define ACTION_UNSET		5
#define ACTION_BATCH		6

#define BATCH_STDIN_BUF_INCREMENT		1024

static void do_list(struct kvc_context *ctx)
{
	struct kvc_enum_context *ectx;
	const union conf_item *item;

	ectx = kvc_enum_init(ctx);

	while (!kvc_enum_next(ectx, &item))
		printf("%s=%s\n", item->key, item->value);

	kvc_enum_end(ectx);
}

static void do_count(struct kvc_context *ctx)
{
	printf("%zd\n", kvc_get_count(ctx));
}

static int do_get(struct kvc_context *ctx)
{
	const char *value, *idxval;

	value = kvc_get(ctx, key);

	if (!value) {
		if (!quiet)
			fprintf(stderr, "key \"%s\" not found\n", key);

		return 1;
	}

	if (findex < 0) {
		printf("%s\n", value);
	} else {
		idxval = dat_get_indexed_value(value, findex);
		if (idxval) {
			printf("%s\n", idxval);
			dat_free_value(idxval);
		}
	}

	return 0;
}

static int do_set(struct kvc_context *ctx)
{
	const char *oldval, *newval;
	int ret;

	newval = value;

	if (findex >= 0) {
		oldval = kvc_get(ctx, key);
		if (!oldval)
			oldval = "";

		newval = dat_set_indexed_value(oldval, findex, value);
		if (!newval) {
			if (!quiet)
				fprintf(stderr, "no memory\n");

			return 1;
		}


	}

	ret = kvc_set(ctx, key, newval);

	if (findex >= 0)
		dat_free_value(newval);

	if (ret) {
		if (!quiet)
			fprintf(stderr, "failed to set key \"%s\"\n", key);

		return 1;
	}

	kvc_commit(ctx);
	return 0;
}

static void do_unset(struct kvc_context *ctx)
{
	kvc_unset(ctx, key);
	kvc_commit(ctx);
}

static int do_batch(struct kvc_context *ctx)
{
	ssize_t ret;
	char *buf = NULL, *nbuf;
	size_t capacity = 0, len = 0;
	struct kvc_context *ctx2;
	struct kvc_enum_context *ectx;
	const union conf_item *item;

	do {
		if (capacity == len) {
			nbuf = realloc(buf,
				capacity + BATCH_STDIN_BUF_INCREMENT);
			if (!nbuf) {
				if (buf)
					free(buf);

				if (!quiet)
					fprintf(stderr, "no memory\n");

				return ENOMEM;
			}

			buf = nbuf;
			capacity += BATCH_STDIN_BUF_INCREMENT;
		}

		ret = read(STDIN_FILENO, buf + len, capacity - len);
		if (ret < 0) {
			if (!quiet)
				fprintf(stderr, "error reading from stdin\n");

			free(buf);
			return errno;
		}

		if (!ret)
			break;

		len += ret;
	} while (1);

	ctx2 = kvc_load_buf_opt(buf, len, DATCONF_LF_FLAGS, dat_nostrip_list);
	if (!ctx2) {
		if (!quiet)
			fprintf(stderr, "no memory\n");

		free(buf);
		return ENOMEM;
	}

	free(buf);

	ectx = kvc_enum_init(ctx2);

	while (!kvc_enum_next(ectx, &item))
		kvc_set(ctx, item->key, item->value);

	kvc_enum_end(ectx);

	kvc_commit(ctx);

	kvc_unload(ctx2);

	return 0;
}

static int do_action(void)
{
	struct kvc_context *ctx;
	int ret = 0;

	ctx = kvc_load_opt(file, DATCONF_LF_FLAGS, dat_nostrip_list);
	if (!ctx) {
		if (!quiet)
			fprintf(stderr, "failed to open %s\n", file);
		return 1;
	}

	switch (action) {
	case ACTION_LIST:
		do_list(ctx);
		break;
	case ACTION_COUNT:
		do_count(ctx);
		break;
	case ACTION_GET:
		ret = do_get(ctx);
		break;
	case ACTION_SET:
		ret = do_set(ctx);
		break;
	case ACTION_UNSET:
		do_unset(ctx);
		break;
	case ACTION_BATCH:
		ret = do_batch(ctx);
		break;
	default:
		/* should not reach here */
		break;
	}

	kvc_unload(ctx);

	return ret;
}

static void usage(const char *prog, FILE *con)
{
	const char *progname;
	size_t len;

	len = strlen(prog);
	progname = prog + len - 1;

	while (progname > prog) {
		if (*progname == '\\' || *progname == '/') {
			progname++;
			break;
		}

		progname--;
	}

	fprintf(con,
		"Usage: %s [<options>] [--] <command>\n"
		"\n"
		"Options:\n"
		"\t-h          display this help\n"
		"\t-f <file>   file to be edited\n"
		"\t-n <name>   file specified by name to be edited\n"
		"\t-i <index>  file specified by index to be edited\n"
		"\t-I <ord>    file specified by ordinal to be edited\n"
		"\t-q          don't display error message\n"
		"\n"
		"Commands:\n"
		"\tlist\n"
		"\tcount\n"
		"\tget <key> [index]\n"
		"\tset <key> [index] <value>\n"
		"\tunset <key>\n"
		"\tbatch\n"
		"\n"
		"Notes:\n"
		"\t-f, -n and -i are mutually exclusive and only one can be "
		"specified\n"
		"\n"
		"\tfor batch command, a list of <key>=<value> lines are "
		"accepted from stdin\n"
		"\n",
		progname
	);
}

static void get_arg_value(int argc, char *argv[], int *idx, char **result)
{
	if (argv[*idx][2]) {
		*result = &argv[*idx][2];
		return;
	}

	if (*idx + 1 >= argc)
		goto error_out;

	if (argv[*idx + 1][0] == '-')
		goto error_out;

	*result = argv[*idx + 1];
	(*idx)++;

	return;

error_out:
	fprintf(stderr, "missing value for argument -%c\n", argv[*idx][1]);
	exit(EINVAL);
}

static int parse_cmd(int argc, char *argv[])
{
	if (!strcmp(argv[0], "list")) {
		action = ACTION_LIST;
		return 0;
	}

	if (!strcmp(argv[0], "count")) {
		action = ACTION_COUNT;
		return 0;
	}

	if (!strcmp(argv[0], "get")) {
		if (argc < 2) {
			fprintf(stderr, "missing <key>\n");
			return -EINVAL;
		}

		key = argv[1];

		if (argc > 2) {
			if (!isdigit(argv[2][0])) {
				fprintf(stderr, "invalid index \"%s\"\n",
					argv[2]);
				return -EINVAL;
			}

			findex = strtoul(argv[2], NULL, 10);
		}

		action = ACTION_GET;
		return 0;
	}

	if (!strcmp(argv[0], "set")) {
		if (argc < 3) {
			fprintf(stderr, "missing <key> and/or <value>\n");
			return -EINVAL;
		}

		key = argv[1];

		if (argc > 3) {
			if (!isdigit(argv[2][0])) {
				fprintf(stderr, "invalid index \"%s\"\n",
					argv[2]);
				return -EINVAL;
			}

			findex = strtoul(argv[2], NULL, 10);
			value = argv[3];
		} else {
			value = argv[2];
		}

		action = ACTION_SET;
		return 0;
	}

	if (!strcmp(argv[0], "unset")) {
		if (argc < 2) {
			fprintf(stderr, "missing <key>\n");
			return -EINVAL;
		}

		key = argv[1];
		action = ACTION_UNSET;
		return 0;
	}

	if (!strcmp(argv[0], "batch")) {
		action = ACTION_BATCH;
		return 0;
	}

	fprintf(stderr, "unknown command \"%s\"\n", argv[0]);
	return -EINVAL;
}

static void parse_args(int argc, char *argv[])
{
	int i;
	int argc_cmd = -1;
	char *fn = NULL, *name = NULL, *index = NULL, *ord = NULL;
	int ex = 0;

	if (argc == 1) {
		usage(argv[0], stdout);
		exit(0);
	}

	for (i = 1; i < argc; i++) {
		if (argv[i][0] != '-') {
			argc_cmd = i;
			break;
		}

		if (argv[i][1] == '-' && argv[i][2] == 0) {
			argc_cmd = i + 1;
			break;
		}

		switch (argv[i][1]) {
		case 'h':
			usage(argv[0], stdout);
			exit(0);
		case 'f':
			get_arg_value(argc, argv, &i, &fn);
			ex++;
			break;
		case 'n':
			get_arg_value(argc, argv, &i, &name);
			ex++;
			break;
		case 'i':
			get_arg_value(argc, argv, &i, &index);
			ex++;
			break;
		case 'I':
			get_arg_value(argc, argv, &i, &ord);
			ex++;
			break;
		case 'q':
			quiet = 1;
			break;
		default:
			fprintf(stderr, "invalid argument: -%c\n", argv[i][1]);
			goto error_out;
		}
	}

	if (!ex) {
		fprintf(stderr, "one of -f, -n, -i or -I must be set\n\n");
		goto error_out;

	}

	if (ex > 1) {
		fprintf(stderr, "only one of -f, -n, -i and -I can be used\n");
		goto error_out;
	}

	if (name) {
		file = get_dat_path_by_name(name);
	} else if (index) {
		if (!isdigit(index[0])){
			fprintf(stderr, "index must be an integer\n");
			goto error_out;
		}

		file = get_dat_path_by_index(strtoul(index, NULL, 10));
	} else if (ord) {
		if (!isdigit(ord[0])){
			fprintf(stderr, "ordinal must be an integer\n");
			goto error_out;
		}

		file = get_dat_path_by_ord(strtoul(ord, NULL, 10));
	} else {
		file = fn;
	}

	if (!file) {
		fprintf(stderr, "missing file to be edited\n");
		goto error_out;
	}

	if (argc_cmd < 0 || argc_cmd >= argc) {
		fprintf(stderr, "missing command\n");
		goto error_out;
	}

	if (!parse_cmd(argc - argc_cmd, argv + argc_cmd))
		return;

error_out:
	fprintf(stderr, "\n");
	usage(argv[0], stderr);
	exit(EINVAL);
}

#ifdef DEBUG
static void dump_args(void)
{
	static const char *action_names[] = {
		[ACTION_LIST] = "list",
		[ACTION_COUNT] = "count",
		[ACTION_GET] = "get",
		[ACTION_SET] = "set",
		[ACTION_UNSET] = "unset",
		[ACTION_BATCH] = "batch"
	};

	printf("file: \"%s\"\n", file);
	printf("quiet: %s\n", quiet ? "yes" : "no");
	printf("action: %s\n", action_names[action]);
	printf("key: \"%s\"\n", key);
	printf("value: \"%s\"\n", value);
	printf("index: %d\n", findex);

	printf("\n");
}
#endif

int main(int argc, char *argv[])
{
	parse_args(argc, argv);

#ifdef DEBUG
	dump_args();
#endif

	return do_action();
}
