// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Author: Abby Cin
 * Mail: abbytsing@gmail.com
 * Create Time: 2024-03-10 19:08:27
 */

#include "linux/bitmap.h"
#include "linux/blkdev.h"
#include "linux/cdev.h"
#include "linux/device.h"
#include "linux/device/class.h"
#include "linux/err.h"
#include "linux/fs.h"
#include "linux/gfp_types.h"
#include "linux/list.h"
#include "linux/mutex.h"
#include "mbdev.h"
#include "ctrl.h"

static const char *g_ctrl_name = "mbdev_ctrl";
static struct bdev_ctrl g_ctrl;

static int ctrl_open(struct inode *inode, struct file *fp)
{
	mutex_lock(&g_ctrl.mtx);
	if (!fp->private_data)
		fp->private_data = &g_ctrl;
	mutex_unlock(&g_ctrl.mtx);
	return 0;
}

static int ctrl_release(struct inode *inode, struct file *fp)
{
	return 0;
}

static long ctrl_ioctl(struct file *fp, unsigned int ops, unsigned long data)
{
	struct bdev_ctrl *c = fp->private_data;
	int rc = -EFAULT;
	struct ctrl_add_cmd add_cmd;
	struct ctrl_list_cmd *list_cmd;
	struct ctrl_del_cmd del_cmd;
	void __user *arg = (void __user *)data;

	mutex_lock(&c->mtx);
	switch (ops) {
	case BDEV_CTRL_ADD:
		if (copy_from_user(&add_cmd, arg, sizeof(add_cmd))) {
			debug("copy_from_user fail");
			goto err;
		}
		rc = bdev_add(c, &add_cmd);
		if (rc) {
			debug("add_bdev fail, rc %d", rc);
			goto err;
		}
		break;
	case BDEV_CTRL_LIST:
		list_cmd = bdev_list(c);
		if (!list_cmd) {
			debug("can't get bdev list");
			goto err;
		}
		if (copy_to_user(arg, list_cmd, list_cmd->size)) {
			debug("copy_to_user fail");
			goto err;
		}
		vfree(list_cmd);
		break;
	case BDEV_CTRL_DEL:
		if (copy_from_user(&del_cmd, arg, sizeof(del_cmd))) {
			debug("copy_from_user fail");
			goto err;
		}
		rc = bdev_del(c, &del_cmd);
		if (rc) {
			debug("del_bdev fail");
			goto err;
		}
		break;
	default:
		rc = -EINVAL;
		goto err;
	}

	rc = 0;
err:
	mutex_unlock(&c->mtx);
	return rc;
}

static ssize_t
ctrl_read(struct file *fp, char __user *buf, size_t size, loff_t *off)
{
	return -ENOENT;
}

static ssize_t
ctrl_write(struct file *fp, const char __user *buf, size_t size, loff_t *off)
{
	return -ENOENT;
}

static struct file_operations g_ctrl_ops = {
	.open = ctrl_open,
	.release = ctrl_release,
	.unlocked_ioctl = ctrl_ioctl,
	.read = ctrl_read,
	.write = ctrl_write,
	.owner = THIS_MODULE,
};

static __init int ctrl_init(void)
{
	memset(&g_ctrl, 0, sizeof(g_ctrl));
	int rc = alloc_chrdev_region(&g_ctrl.cdevno, 0, 1, g_ctrl_name);
	if (rc) {
		debug("alloc_chrdev_region fail rc %d", rc);
		return -EFAULT;
	}

	cdev_init(&g_ctrl.dev, &g_ctrl_ops);
	rc = cdev_add(&g_ctrl.dev, g_ctrl.cdevno, 1);
	if (rc) {
		debug("cdev_add fail rc %d", rc);
		goto err;
	}

	if (IS_ERR(g_ctrl.class = class_create(g_ctrl_name))) {
		rc = (int)PTR_ERR(g_ctrl.class);
		debug("class_create fail rc %d", rc);
		goto err1;
	}

	if (IS_ERR(g_ctrl.device = device_create(g_ctrl.class,
						 NULL,
						 g_ctrl.cdevno,
						 NULL,
						 g_ctrl_name))) {
		rc = (int)PTR_ERR(g_ctrl.device);
		debug("device_create fail rc %d", rc);
		goto err1;
	}

	g_ctrl.bdev_major = register_blkdev(0, BDEV_NAME);
	if (g_ctrl.bdev_major <= 0) {
		debug("register_blkdev fail");
		goto err1;
	}
	g_ctrl.minor_map = bitmap_zalloc(BDEV_MAX_SZ, GFP_KERNEL);
	if (!g_ctrl.minor_map) {
		debug("bitmap_alloc fail");
		goto err1;
	}
	INIT_LIST_HEAD(&g_ctrl.bdev_head);
	mutex_init(&g_ctrl.mtx);
	debug("hello mbdev");

	return 0;
err1:
	if (!IS_ERR(g_ctrl.class))
		class_destroy(g_ctrl.class);
	cdev_del(&g_ctrl.dev);
err:
	unregister_chrdev_region(g_ctrl.cdevno, 1);
	return rc;
}

static __exit void ctrl_exit(void)
{
	struct list_head *pos, *n;
	struct my_bdev *bdev;

	list_for_each_safe(pos, n, &g_ctrl.bdev_head)
	{
		bdev = list_entry(pos, struct my_bdev, link);
		bdev_destroy(bdev);
		list_del(pos);
	}
	bitmap_free(g_ctrl.minor_map);
	mutex_destroy(&g_ctrl.mtx);
	device_destroy(g_ctrl.class, g_ctrl.cdevno);
	class_destroy(g_ctrl.class);
	cdev_del(&g_ctrl.dev);
	unregister_blkdev(g_ctrl.bdev_major, BDEV_NAME);
	unregister_chrdev_region(g_ctrl.cdevno, 1);
	debug("goodby mbdev");
}

module_init(ctrl_init);
module_exit(ctrl_exit);
MODULE_DESCRIPTION("a simple in memory block device driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("abbycin (abbytsing@gmail.com)");
MODULE_VERSION("1.0");