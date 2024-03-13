// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Author: Abby Cin
 * Mail: abbytsing@gmail.com
 * Create Time: 2024-03-13 15:26:17
 */

#ifndef CTRL_1710314777_H_
#define CTRL_1710314777_H_

#include <linux/ioctl.h>
#ifndef UAPI
#include <linux/types.h>
#else
#include <stddef.h>
#endif

#define BDEV_NAME_SZ 20
#define BDEV_MAX_SZ 256U

struct my_bdev_info {
        size_t capacity;
        int refcnt;
        unsigned minor;
        char name[BDEV_NAME_SZ];
};

struct ctrl_add_cmd {
        unsigned qdepth;
        unsigned nr_queue;
        size_t capacity;
};

struct ctrl_list_cmd {
        size_t size; // the size including `size`
        struct my_bdev_info bdevs[];
};

struct ctrl_del_cmd {
        unsigned minor;
};

#define MAGIC ('a' | 'b' | 'b' | 'y' | 'b' | 'd' | 'v')
#define BDEV_CTRL_ADD _IOW(MAGIC, 1, struct ctrl_add_cmd *)
#define BDEV_CTRL_LIST _IOR(MAGIC, 2, struct ctrl_list_cmd *)
#define BDEV_CTRL_DEL _IOW(MAGIC, 3, struct ctrl_del_cmd *)

#endif // CTRL_1710314777_H_
