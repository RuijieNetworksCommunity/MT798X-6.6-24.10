// SPDX-License-Identifier:	LGPL-2.1
/*
 * CLI tool for editing key-value based file
 *
 * Copyright (C) 2019 MediaTek Inc. All Rights Reserved.
 *
 * Author: Weijie Gao <weijie.gao@mediatek.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "libkvcutil.h"

static int flags = LF_KEY_CASE_SENSITIVE;
static char *file;
static int eol = -1;
static int quiet;
static int action;
static char *key;
static char *value;
static int ord = -1;

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
	const char *value;

	if (ord >= 0)
		value = kvc_get_indexed(ctx, key, ord);
	else
		value = kvc_get(ctx, key);

	if (value) {
		printf("%s\n", value);
		return 0;
	}

	if (!quiet) {
		if (ord >= 0) {
			fprintf(stderr,
				"key \"%s\" at ordinal %u not found\n", key,
				ord);
		} else {
			fprintf(stderr, "key \"%s\" not found\n", key);
		}
	}

	return 1;
}

static int do_set(struct kvc_context *ctx)
{
	int ret;

	if (ord >= 0)
		ret = kvc_set_indexed(ctx, key, ord, value);
	else
		ret = kvc_set(ctx, key, value);

	if (ret) {
		if (!quiet) {
			if (ord >= 0) {
				fprintf(stderr,
					"failed to set key \"%s\" at ordinal %u\n",
					key, ord);
			} else {
				fprintf(stderr,
					"failed to set key \"%s\"\n", key);
			}
		}

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

	/* read from stdin */
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

	/* convert stdin in to key-values pairs, using the same flags */
	ctx2 = kvc_load_buf(buf, len, flags);
	if (!ctx2) {
		if (!quiet)
			fprintf(stderr, "no memory\n");

		free(buf);
		return ENOMEM;
	}

	/* buf if no longer used */
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

	ctx = kvc_load(file, flags);
	if (!ctx) {
		if (!quiet)
			fprintf(stderr, "failed to open %s\n", file);
		return 1;
	}

	if (eol >= 0)
		kvc_set_default_eol(ctx, eol);

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
		"\t-q          don't display error message\n"
		"\t-l          lock file during operation\n"
		"\t-s          strip whitespaces around keys\n"
		"\t-S          strip whitespaces around values\n"
		"\t-c          remove comment lines start with '#'\n"
		"\t-n          remove non-key-value-pair lines\n"
		"\t-e          preserve line ending on committing\n"
		"\t-a          assume key is case insensitive\n"
		"\t-r <eol>    set default line ending (cr, lf, crlf)\n"
		"\t-i <ord>    access to the key with specified ordinal\n"
		"\n"
		"Commands:\n"
		"\tlist\n"
		"\tcount\n"
		"\tget <key>\n"
		"\tset <key> <value>\n"
		"\tunset <key>\n"
		"\tbatch\n"
		"\n"
		"Notes:\n"
		"\tfor batch command, a list of <key>=<value> lines are "
		"accepted from stdin\n"
		"\n",
		progname
	);
}

/*
 * two forms to be processed:
 *     1: -O<value>
 *     2: -O <value>
 */
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
		action = ACTION_GET;
		return 0;
	}

	if (!strcmp(argv[0], "set")) {
		if (argc < 3) {
			fprintf(stderr, "missing <key> and/or <value>\n");
			return -EINVAL;
		}

		key = argv[1];
		value = argv[2];
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
	char *eol_str, *ord_str;

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
			get_arg_value(argc, argv, &i, &file);
			break;
		case 'q':
			quiet = 1;
			break;
		case 'l':
			flags |= LF_FLOCK;
			break;
		case 's':
			flags |= LF_STRIP_KEY_WHITESPACE;
			break;
		case 'S':
			flags |= LF_STRIP_VALUE_WHITESPACE;
			break;
		case 'c':
			flags |= LF_STRIP_COMMENTS;
			break;
		case 'n':
			flags |= LF_STRIP_INVALID;
			break;
		case 'e':
			flags |= LF_PRESERVE_LINE_ENDING;
			break;
		case 'a':
			flags &= ~LF_KEY_CASE_SENSITIVE;
			break;
		case 'r':
			get_arg_value(argc, argv, &i, &eol_str);
			if (!strcmp(eol_str, "lf")) {
				eol = EOL_LF;
			} else if (!strcmp(eol_str, "cr")) {
				eol = EOL_CR;
			} else if (!strcmp(eol_str, "crlf")) {
				eol = EOL_CRLF;
			} else {
				fprintf(stderr, "invalid line ending: %s\n",
					eol_str);
				goto error_out;
			}
			break;
		case 'i':
			get_arg_value(argc, argv, &i, &ord_str);
			ord = strtoul(ord_str, NULL, 10);
			break;
		default:
			fprintf(stderr, "invalid argument: -%c\n", argv[i][1]);
			goto error_out;
		}
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

	static const char *eol_names[] = {
		[EOL_LF] = "lf",
		[EOL_CR] = "cr",
		[EOL_CRLF] = "crlf"
	};

	printf("file: \"%s\"\n", file);
	printf("quiet: %s\n", quiet ? "yes" : "no");
	printf("action: %s\n", action_names[action]);
	printf("key: \"%s\"\n", key);
	printf("value: \"%s\"\n", value);

	if (eol >= 0)
		printf("eol: %s\n", eol_names[eol]);

	printf("flags:\n");

	if (flags & LF_FLOCK)
		printf("\tLF_FLOCK\n");

	if (flags & LF_STRIP_WHITESPACE)
		printf("\tLF_STRIP_WHITESPACE\n");

	if (flags & LF_STRIP_COMMENTS)
		printf("\tLF_STRIP_COMMENTS\n");

	if (flags & LF_STRIP_INVALID)
		printf("\tLF_STRIP_INVALID\n");

	if (flags & LF_PRESERVE_LINE_ENDING)
		printf("\tLF_PRESERVE_LINE_ENDING\n");

	if (flags & LF_KEY_CASE_SENSITIVE)
		printf("\tLF_KEY_CASE_SENSITIVE\n");

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
