// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Author: Abby Cin
 * Mail: abbytsing@gmail.com
 * Create Time: 2024-03-10 19:08:21
 */

#ifndef MBDEV_1710068901_H_
#define MBDEV_1710068901_H_

#include "ctrl.h"
#include <linux/export.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/cdev.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/bitmap.h>
#include <linux/math.h>
#include <linux/vmalloc.h>

#define BDEV_BLOCK_SZ 4096U
#define BDEV_NAME "mbdev"

#define debug(fmt, ...)                                                        \
	printk(KERN_NOTICE "mbdev %s:%d " fmt "\n",                            \
	       __func__,                                                       \
	       __LINE__,                                                       \
	       ##__VA_ARGS__)

struct my_bdev {
	struct list_head link;
        struct my_bdev_info info;
	struct gendisk *disk;
        struct blk_mq_tag_set tag_set;
        char *data; // memory for IO
};

struct bdev_ctrl {
        dev_t cdevno;
        unsigned int bdev_major;
        unsigned long *minor_map;
        struct cdev dev;
        struct mutex mtx;
        struct class *class;
        struct device *device;
        struct list_head bdev_head;
};

int bdev_add(struct bdev_ctrl *ctrl, struct ctrl_add_cmd *cmd);

struct ctrl_list_cmd *bdev_list(struct bdev_ctrl *ctrl);

int bdev_del(struct bdev_ctrl *ctrl, struct ctrl_del_cmd *cmd);

void bdev_destroy(struct my_bdev *bdev);

#endif // MBDEV_1710068901_H_
