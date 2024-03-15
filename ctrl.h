// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Author: Abby Cin
 * Mail: abbytsing@gmail.com
 * Create Time: 2024-03-13 15:26:17
 */

#ifndef CTRL_1710314777_H_
#define CTRL_1710314777_H_

#include <linux/ioctl.h>
#include <linux/types.h>

#define BDEV_NAME_SZ 20
#define BDEV_MAX_SZ 256U

struct my_bdev_info {
        __u64 capacity;
        __s32 refcnt;
        __u32 minor;
        __s8 name[BDEV_NAME_SZ];
};

struct ctrl_add_cmd {
        __u32 qdepth;
        __u32 nr_queue;
        __u64 capacity;
};

struct ctrl_list_cmd {
        __u64 size; // the size including `size`
        struct my_bdev_info bdevs[];
};

struct ctrl_del_cmd {
        __u32 minor;
};

#define MAGIC ('a' | 'b' | 'b' | 'y' | 'b' | 'd' | 'v')
#define BDEV_CTRL_ADD _IOW(MAGIC, 1, struct ctrl_add_cmd *)
#define BDEV_CTRL_LIST _IOR(MAGIC, 2, struct ctrl_list_cmd *)
#define BDEV_CTRL_DEL _IOW(MAGIC, 3, struct ctrl_del_cmd *)

#endif // CTRL_1710314777_H_
