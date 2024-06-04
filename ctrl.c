// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Author: Abby Cin
 * Mail: abbytsing@gmail.com
 * Create Time: 2024-03-13 15:24:46
 */

#include "ctrl.h"
#include <stdio.h>
#include <getopt.h>
#include <fcntl.h>
#include <unistd.h>
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>

#define ctrl_file "/dev/mbdev_ctrl"

#define debug(fmt, ...)                                                        \
	fprintf(stderr, "%s:%d " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)

enum opt_group {
	OPT_INVALID,
	OPT_ADD,
	OPT_LIST,
	OPT_DEL,
};

static void handle_add(int fd, size_t bytes, unsigned nq, unsigned qd)
{
	struct ctrl_add_cmd cmd = {
		.capacity = bytes,
		.nr_queue = nq,
		.qdepth = qd,
	};

	int rc = ioctl(fd, BDEV_CTRL_ADD, &cmd);
	if (rc != 0)
		debug("add fail rc %d error %s", rc, strerror(errno));
	else
		debug("add dev with capacity %zu nr_queue %u queue_depth %u",
		      bytes,
		      nq,
		      qd);
}

static int _cmp(const void *l, const void *r)
{
	struct my_bdev_info *ll = (void *)l;
	struct my_bdev_info *rr = (void *)r;

	return ll->minor - rr->minor;
}

static void handle_list(int fd)
{
	struct ctrl_list_cmd *cmd;
	struct my_bdev_info *info;

	cmd = malloc(sizeof(*cmd) + BDEV_MAX_SZ * sizeof(*info));
	if (!cmd) {
		debug("list fail, can't alloc memory");
		return;
	}
	int rc = ioctl(fd, BDEV_CTRL_LIST, cmd);
	if (rc != 0) {
		debug("list fail rc %d error %s", rc, strerror(errno));
		goto err;
	}
	int n = (cmd->size - sizeof(cmd->size)) / sizeof(*info);
	qsort(cmd->bdevs, n, sizeof(*info), _cmp);
	for (int i = 0; i < n; ++i) {
		info = &cmd->bdevs[i];
		printf("%-12s capacity %zu refcnt %d\n",
		       info->name,
		       info->capacity,
		       info->refcnt);
	}
err:
	free(cmd);
}

static void handle_del(int fd, unsigned minor)
{
	if (minor == -1) {
		debug("bad minor number");
		return;
	}

	struct ctrl_del_cmd cmd = { .minor = minor };
	int rc = ioctl(fd, BDEV_CTRL_DEL, &cmd);
	if (rc != 0)
		debug("del %u fail rc %d error %s", minor, rc, strerror(errno));
}

int main(int argc, char *argv[])
{
	optind = 0;
	int opt = 0;
	enum opt_group grp = OPT_INVALID;
	size_t bytes = 1UL << 20;
	unsigned nq = 1;
	unsigned qd = 128;
	unsigned minor = -1;

	while ((opt = getopt(argc, argv, "as:q:d:lr:")) != -1) {
		switch (opt) {
		case 'a':
			grp = OPT_ADD;
			break;
		case 's':
			bytes = strtoull(optarg, NULL, 10);
			break;
		case 'q':
			nq = strtoul(optarg, NULL, 10);
			break;
		case 'd':
			qd = strtoul(optarg, NULL, 10);
			break;
		case 'l':
			grp = OPT_LIST;
			break;
		case 'r':
			grp = OPT_DEL;
			minor = strtoul(optarg, NULL, 10);
			break;
		default:
			break;
		}
	}

	if (grp == OPT_INVALID) {
		debug("%s [-a [-s size] [-q nr_queue] [-d qdpeth]] [-l] [-r "
		      "minor]",
		      argv[0]);
		return 1;
	}

	int fd = open(ctrl_file, O_RDWR);
	if (fd < 0) {
		debug("can't open %s rc %d", ctrl_file, fd);
		return 1;
	}

	switch (grp) {
	case OPT_ADD:
		handle_add(fd, bytes, nq, qd);
		break;
	case OPT_LIST:
		handle_list(fd);
		break;
	case OPT_DEL:
		handle_del(fd, minor);
		break;
	default:
		break;
	}

	close(fd);
	return 0;
}
