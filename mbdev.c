// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Author: Abby Cin
 * Mail: abbytsing@gmail.com
 * Create Time: 2024-03-10 19:08:14
 */

#include "mbdev.h"

static int mbdev_open(struct gendisk *disk, blk_mode_t mode)
{
	struct my_bdev *bdev = disk->private_data;

	__atomic_fetch_add(&bdev->info.refcnt, 1, __ATOMIC_SEQ_CST);
	return 0;
}

static void mbdev_release(struct gendisk *disk)
{
	struct my_bdev *bdev = disk->private_data;

	__atomic_fetch_sub(&bdev->info.refcnt, 1, __ATOMIC_SEQ_CST);
}

#ifndef REQUESTS_BASED
static void mbdev_submit_bio(struct bio *bio)
{
	struct my_bdev *bdev = bio->bi_bdev->bd_disk->private_data;
	struct bio_vec v;
	struct bvec_iter iter;
	loff_t pos = bio->bi_iter.bi_sector << SECTOR_SHIFT;
	loff_t size = bdev->info.capacity;
	unsigned long start_time = bio_start_io_acct(bio);

	bio_for_each_segment(v, bio, iter)
	{
		unsigned len = v.bv_len;
		void *buf = (char *)page_address(v.bv_page) + v.bv_offset;

		if (pos + len > size) {
			bio->bi_status = BLK_STS_IOERR;
			break;
		}
		if (bio_data_dir(bio) == WRITE)
			memcpy(bdev->data + pos, buf, len);
		else
			memcpy(buf, bdev->data + pos, len);
		pos += len;
	}
	bio_end_io_acct(bio, start_time);
	bio_endio(bio);
}

#endif

static struct block_device_operations bdev_ops = {
	.owner = THIS_MODULE,
	.open = mbdev_open,
	.release = mbdev_release,
#ifndef REQUESTS_BASED
	.submit_bio = mbdev_submit_bio,
#endif
};

static blk_status_t mbdev_queue_rq(struct blk_mq_hw_ctx *hctx,
				   const struct blk_mq_queue_data *data)
{
	struct request *rq = data->rq;
	struct bio_vec vec;
	struct req_iterator iter;
	struct my_bdev *bdev = rq->q->queuedata;
	loff_t pos = blk_rq_pos(rq) << SECTOR_SHIFT;
	loff_t cap = bdev->info.capacity; // already multiple SECTOR
	enum req_op op = req_op(rq);
	blk_status_t rc = BLK_STS_OK;

	switch (op) {
	case REQ_OP_READ:
		// fall through
	case REQ_OP_WRITE:
		break;
	default:
		return BLK_STS_IOERR;
	}

	cant_sleep();

	blk_mq_start_request(rq);

	u64 nbytes = 0;
	rq_for_each_segment(vec, rq, iter)
	{
		u64 len = vec.bv_len;
		void *buf = (char *)page_address(vec.bv_page) + vec.bv_offset;

		if (pos + len > cap) {
			// len = cap - pos;
			rc = BLK_STS_IOERR;
			break;
		}

		if (op == REQ_OP_WRITE)
			memcpy(bdev->data + pos, buf, len);
		else if (op == REQ_OP_READ)
			memcpy(buf, bdev->data + pos, len);
		pos += len;
		nbytes += len;
	}

	blk_mq_end_request(rq, rc);
	debug("nbytes %llu last %d op %s",
	      nbytes,
	      data->last,
	      op == REQ_OP_WRITE ? "write" : "read");
	return rc;
}

static struct blk_mq_ops mq_ops = {
	.queue_rq = mbdev_queue_rq,
};

int bdev_add(struct bdev_ctrl *ctrl, struct ctrl_add_cmd *cmd)
{
	int rc = -ENOMEM;
	char name[BDEV_NAME_SZ] = { 0 };
	unsigned minor = find_first_zero_bit(ctrl->minor_map, BDEV_MAX_SZ);
	if (minor == BDEV_MAX_SZ)
		return rc;

	snprintf(name, sizeof(name), "%s_%u", BDEV_NAME, minor);
	struct my_bdev *bdev = kzalloc(sizeof(struct my_bdev), GFP_KERNEL);
	if (!bdev) {
		debug("alloc bdev fail");
		return rc;
	}
	INIT_LIST_HEAD(&bdev->link);
	bdev->info.capacity = round_up(cmd->capacity, SECTOR_SIZE);
	bdev->info.minor = minor;
	memcpy(bdev->info.name, name, sizeof(name));
	bdev->data = vmalloc(bdev->info.capacity);
	if (!bdev->data) {
		debug("can't alloc %llu for %s", bdev->info.capacity, name);
		goto err;
	}

	debug("add_cmd => cap %llu nr_queue %u qdepth %u",
	      cmd->capacity,
	      cmd->nr_queue,
	      cmd->qdepth);
	// init tag set and disk
	bdev->tag_set.ops = &mq_ops;
	bdev->tag_set.nr_hw_queues = cmd->nr_queue % num_online_cpus();
	bdev->tag_set.queue_depth = cmd->qdepth;
	bdev->tag_set.numa_node = NUMA_NO_NODE;
	bdev->tag_set.flags = BLK_MQ_F_SHOULD_MERGE | BLK_MQ_F_STACKING;
	bdev->tag_set.cmd_size = 0;
	bdev->tag_set.driver_data = bdev;

	rc = blk_mq_alloc_tag_set(&bdev->tag_set);
	if (rc) {
		debug("blk_mq_alloc_tag_set fail rc %d", rc);
		goto err1;
	}

	bdev->disk = blk_mq_alloc_disk(&bdev->tag_set, bdev);
	if (!bdev->disk) {
		debug("blk_mq_alloc_disk fail");
		goto err2;
	}

	// NOTE: comment in `struct gendisk` says, the following three fields
	// should not be set by new drivers
	bdev->disk->major = ctrl->bdev_major;
	bdev->disk->first_minor = minor + 1;
	bdev->disk->minors = minor + 1;

	bdev->disk->flags |= GENHD_FL_NO_PART; // only one parttion
	bdev->disk->fops = &bdev_ops;
	bdev->disk->private_data = bdev;
	snprintf(bdev->disk->disk_name,
		 sizeof(bdev->disk->disk_name),
		 "%s",
		 name);
	// NOTE: size must be power of SECTOR_SIZE (512)
	set_capacity(bdev->disk, bdev->info.capacity / SECTOR_SIZE);

	blk_queue_physical_block_size(bdev->disk->queue, SECTOR_SIZE);
	blk_queue_logical_block_size(bdev->disk->queue, SECTOR_SIZE);

	blk_queue_max_hw_sectors(bdev->disk->queue, BLK_DEF_MAX_SECTORS);
	blk_queue_flag_set(QUEUE_FLAG_NOMERGES, bdev->disk->queue);

	rc = add_disk(bdev->disk);
	if (rc != 0) {
		debug("add_disk fail rc %d", rc);
		put_disk(bdev->disk);
		goto err2;
	}

	bitmap_set(ctrl->minor_map, minor, 1);
	list_add_tail(&bdev->link, &ctrl->bdev_head);
	return 0;

err2:
	blk_mq_free_tag_set(&bdev->tag_set);

err1:
	vfree(bdev->data);
err:
	kfree(bdev);
	return rc;
}
EXPORT_SYMBOL_GPL(bdev_add);

struct ctrl_list_cmd *bdev_list(struct bdev_ctrl *ctrl)
{
	size_t nr = bitmap_weight(ctrl->minor_map, BDEV_MAX_SZ);
	struct ctrl_list_cmd *cmd;
	struct list_head *pos;
	size_t size = sizeof(*cmd) + nr * sizeof(struct my_bdev_info);

	cmd = vzalloc(size);
	if (cmd == NULL) {
		debug("vzalloc fail");
		return NULL;
	}
	int i = 0;
	list_for_each(pos, &ctrl->bdev_head)
	{
		struct my_bdev *bdev = list_entry(pos, struct my_bdev, link);
		memcpy(&cmd->bdevs[i], &bdev->info, sizeof(bdev->info));
		cmd->bdevs[i].refcnt =
			__atomic_load_n(&bdev->info.refcnt, __ATOMIC_SEQ_CST);
		i += 1;
	}
	debug("copy size %lu data %lu", size, size - sizeof(*cmd));
	cmd->size = size;
	return cmd;
}
EXPORT_SYMBOL_GPL(bdev_list);

void bdev_destroy(struct my_bdev *bdev)
{
	del_gendisk(bdev->disk);

	put_disk(bdev->disk);
	blk_mq_free_tag_set(&bdev->tag_set);
	vfree(bdev->data);
	kfree(bdev);
}
EXPORT_SYMBOL_GPL(bdev_destroy);

int bdev_del(struct bdev_ctrl *ctrl, struct ctrl_del_cmd *cmd)
{
	struct list_head *pos;
	struct my_bdev *bdev;
	bool find = false;

	list_for_each(pos, &ctrl->bdev_head)
	{
		bdev = list_entry(pos, struct my_bdev, link);
		if (bdev->info.minor == cmd->minor) {
			find = true;
			break;
		}
	}

	if (!find) {
		debug("invalid cmd minor %u", cmd->minor);
		return -EINVAL;
	}

	if (__atomic_load_n(&bdev->info.refcnt, __ATOMIC_SEQ_CST)) {
		debug("%s is in use", bdev->info.name);
		return -EBUSY;
	}

	list_del(pos);
	bitmap_clear(ctrl->minor_map, bdev->info.minor, 1);
	bdev_destroy(bdev);
	return 0;
}
EXPORT_SYMBOL_GPL(bdev_del);
