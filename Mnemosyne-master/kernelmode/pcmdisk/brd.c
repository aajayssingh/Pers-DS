/*
    Copyright (C) 2011 Computer Sciences Department, 
    University of Wisconsin -- Madison

    ----------------------------------------------------------------------

    This file is part of Mnemosyne: Lightweight Persistent Memory, 
    originally developed at the University of Wisconsin -- Madison.

    Mnemosyne was originally developed primarily by Haris Volos
    with contributions from Andres Jaan Tack.

    ----------------------------------------------------------------------

    Mnemosyne is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation, version 2
    of the License.
 
    Mnemosyne is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, 
    Boston, MA  02110-1301, USA.

### END HEADER ###
*/

/*
 * PCM ram backed block device driver.
 *
 * Copyright (C) 2011 Haris Volos
 *
 * Parts derived from drivers/block/brd.c, copyright of their respective owners.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/major.h>
#include <linux/blkdev.h>
#include <linux/bio.h>
#include <linux/highmem.h>
#include <linux/gfp.h>
#include <linux/radix-tree.h>
#include <linux/buffer_head.h> /* invalidate_bh_lrus() */
#include <linux/delay.h>

#include <asm/uaccess.h>

#include "pcm.h"

#define SECTOR_SHIFT		      9
#define PAGE_SECTORS_SHIFT	      (PAGE_SHIFT - SECTOR_SHIFT)
#define PAGE_SECTORS		      (1 << PAGE_SECTORS_SHIFT)

#define PCMDISK_MAJOR                 240
#define PCMDISK_CONTROL_MAJOR         241

#define CONFIG_BLK_DEV_PCM_COUNT      1 
#define CONFIG_BLK_DEV_PCM_SIZE       2*1024*1024 /* KB */

#define PCMDISK_CRASH                 32000
#define PCMDISK_CRASH_RESET           32001
#define PCMDISK_SET_PCM_BANDWIDTH     32002
#define PCMDISK_GET_PCM_BANDWIDTH     32003
#define PCMDISK_SET_DRAM_BANDWIDTH    32004
#define PCMDISK_GET_DRAM_BANDWIDTH    32005
#define PCMDISK_RESET_STATISTICS      32006
#define PCMDISK_PRINT_STATISTICS      32007

extern int PCM_BANDWIDTH_MB;
extern int DRAM_BANDWIDTH_MB;

static LIST_HEAD(brd_devices);
static DEFINE_MUTEX(brd_devices_mutex);

/* 
 * User-space can control the block device driver via a character device driver.
 */ 


/*
 * Each block pcmdisk device has a radix_tree brd_pages of pages that stores
 * the pages containing the block device's contents. A brd page's ->index is
 * its offset in PAGE_SIZE units. This is similar to, but in no way connected
 * with, the kernel's pagecache or buffer cache (which sit above our block
 * device).
 */
struct brd_device {
	int		brd_number;
	int		brd_refcnt;
	loff_t		brd_offset;
	loff_t		brd_sizelimit;
	unsigned	brd_blocksize;

	struct request_queue	*brd_queue;
	struct gendisk		*brd_disk;
	struct list_head	brd_list;

	/*
	 * Backing store of pages and lock to protect it. This is the contents
	 * of the block device.
	 */
	spinlock_t		brd_lock;
	struct radix_tree_root	brd_pages;
	
	/*
	 * PCM layer
	 */
	pcm_t *pcm;
};

/*
 * Look up and return a brd's page for a given sector.
 */
static struct page *brd_lookup_page(struct brd_device *brd, sector_t sector)
{
	pgoff_t idx;
	struct page *page;

	/*
	 * The page lifetime is protected by the fact that we have opened the
	 * device node -- brd pages will never be deleted under us, so we
	 * don't need any further locking or refcounting.
	 *
	 * This is strictly true for the radix-tree nodes as well (ie. we
	 * don't actually need the rcu_read_lock()), however that is not a
	 * documented feature of the radix-tree API so it is better to be
	 * safe here (we don't have total exclusion from radix tree updates
	 * here, only deletes).
	 */
	rcu_read_lock();
	idx = sector >> PAGE_SECTORS_SHIFT; /* sector to page index */
	page = radix_tree_lookup(&brd->brd_pages, idx);
	rcu_read_unlock();

	BUG_ON(page && page->index != idx);

	return page;
}

/*
 * Look up and return a brd's page for a given sector.
 * If one does not exist, allocate an empty page, and insert that. Then
 * return it.
 */
static struct page *brd_insert_page(struct brd_device *brd, sector_t sector)
{
	pgoff_t idx;
	struct page *page;
	gfp_t gfp_flags;

	page = brd_lookup_page(brd, sector);
	if (page)
		return page;

	/*
	 * Must use NOIO because we don't want to recurse back into the
	 * block or filesystem layers from page reclaim.
	 *
	 * Cannot support XIP and highmem, because our ->direct_access
	 * routine for XIP must return memory that is always addressable.
	 * If XIP was reworked to use pfns and kmap throughout, this
	 * restriction might be able to be lifted.
	 */
	gfp_flags = GFP_NOIO | __GFP_ZERO;
#ifndef CONFIG_BLK_DEV_XIP
	gfp_flags |= __GFP_HIGHMEM;
#endif
	page = alloc_page(gfp_flags);
	if (!page)
		return NULL;

	if (radix_tree_preload(GFP_NOIO)) {
		__free_page(page);
		return NULL;
	}

	spin_lock(&brd->brd_lock);
	idx = sector >> PAGE_SECTORS_SHIFT;
	if (radix_tree_insert(&brd->brd_pages, idx, page)) {
		__free_page(page);
		page = radix_tree_lookup(&brd->brd_pages, idx);
		BUG_ON(!page);
		BUG_ON(page->index != idx);
	} else
		page->index = idx;
	spin_unlock(&brd->brd_lock);

	radix_tree_preload_end();

	return page;
}

/*
 * Free all backing store pages and radix tree. This must only be called when
 * there are no other users of the device.
 */
#define FREE_BATCH 16
static void brd_free_pages(struct brd_device *brd)
{
	unsigned long pos = 0;
	struct page *pages[FREE_BATCH];
	int nr_pages;

	do {
		int i;

		nr_pages = radix_tree_gang_lookup(&brd->brd_pages,
				(void **)pages, pos, FREE_BATCH);

		for (i = 0; i < nr_pages; i++) {
			void *ret;

			BUG_ON(pages[i]->index < pos);
			pos = pages[i]->index;
			ret = radix_tree_delete(&brd->brd_pages, pos);
			BUG_ON(!ret || ret != pages[i]);
			__free_page(pages[i]);
		}

		pos++;

		/*
		 * This assumes radix_tree_gang_lookup always returns as
		 * many pages as possible. If the radix-tree code changes,
		 * so will this have to.
		 */
	} while (nr_pages == FREE_BATCH);
}

/*
 * copy_to_brd_setup must be called before copy_to_brd. It may sleep.
 */
static int copy_to_brd_setup(struct brd_device *brd, sector_t sector, size_t n)
{
	unsigned int offset = (sector & (PAGE_SECTORS-1)) << SECTOR_SHIFT;
	size_t copy;

	copy = min_t(size_t, n, PAGE_SIZE - offset);
	if (!brd_insert_page(brd, sector))
		return -ENOMEM;
	if (copy < n) {
		sector += copy >> SECTOR_SHIFT;
		if (!brd_insert_page(brd, sector))
			return -ENOMEM;
	}
	return 0;
}

/*
 * Copy n bytes from src to the brd starting at sector. Does not sleep.
 */
static void copy_to_brd(pcm_storeset_t *set, 
                        struct brd_device *brd, const void *src,
			sector_t sector, size_t n)
{
	struct page *page;
	void *dst;
	unsigned int offset = (sector & (PAGE_SECTORS-1)) << SECTOR_SHIFT;
	size_t copy;

	copy = min_t(size_t, n, PAGE_SIZE - offset);
	page = brd_lookup_page(brd, sector);
	BUG_ON(!page);

	/* If the PCM layer is in crash mode then don't perform any writes. */
	if (pcm_check_crash(brd->pcm)) {
		printk(KERN_INFO "IGNORE WRITE\n");
		return;
	}
	dst = kmap_atomic(page, KM_USER1);
	pcm_memcpy(set, dst + offset, src, copy);
	//////preempt_disable();
	//memcpy(dst + offset, src, copy);
	//////preempt_enable();
	kunmap_atomic(dst, KM_USER1);

	/* If the PCM layer is in crash mode then don't perform any writes. */
	if (pcm_check_crash(brd->pcm)) {
		return;
	}

	if (copy < n) {
		src += copy;
		sector += copy >> SECTOR_SHIFT;
		copy = n - copy;
		page = brd_lookup_page(brd, sector);
		BUG_ON(!page);

		dst = kmap_atomic(page, KM_USER1);
		pcm_memcpy(set, dst, src, copy);
		//////preempt_disable();
		//memcpy(dst, src, copy);
		//////preempt_enable();
		kunmap_atomic(dst, KM_USER1);
	}
}

/*
 * Copy n bytes to dst from the brd starting at sector. Does not sleep.
 */
static void copy_from_brd(void *dst, struct brd_device *brd,
			sector_t sector, size_t n)
{
	struct page *page;
	void *src;
	unsigned int offset = (sector & (PAGE_SECTORS-1)) << SECTOR_SHIFT;
	size_t copy;

	copy = min_t(size_t, n, PAGE_SIZE - offset);
	page = brd_lookup_page(brd, sector);
	if (page) {
		src = kmap_atomic(page, KM_USER1);
		memcpy(dst, src + offset, copy);
		kunmap_atomic(src, KM_USER1);
	} else
		memset(dst, 0, copy);

	if (copy < n) {
		dst += copy;
		sector += copy >> SECTOR_SHIFT;
		copy = n - copy;
		page = brd_lookup_page(brd, sector);
		if (page) {
			src = kmap_atomic(page, KM_USER1);
			memcpy(dst, src, copy);
			kunmap_atomic(src, KM_USER1);
		} else
			memset(dst, 0, copy);
	}
}

/*
 * Process a single bvec of a bio.
 */
static int brd_do_bvec(struct brd_device *brd, struct page *page,
			unsigned int len, unsigned int off, int rw,
			sector_t sector)
{
	void *mem;
	int err = 0;
	pcm_storeset_t *set;

	if (rw != READ) {
		err = pcm_storeset_create(brd->pcm, &set);
		if (err) {
			goto out;
		}
		err = copy_to_brd_setup(brd, sector, len);
		if (err) {
			goto out;
		}
	}

	mem = kmap_atomic(page, KM_USER0);
	if (rw == READ) {
		copy_from_brd(mem + off, brd, sector, len);
		flush_dcache_page(page);
	} else {
		flush_dcache_page(page);
		copy_to_brd(set, brd, mem + off, sector, len);
	}
	kunmap_atomic(mem, KM_USER0);

out:
	if (rw != READ) {
		pcm_storeset_destroy(set);
	}
	return err;
}

static int brd_make_request(struct request_queue *q, struct bio *bio)
{
	struct block_device *bdev = bio->bi_bdev;
	struct brd_device *brd = bdev->bd_disk->private_data;
	int rw;
	struct bio_vec *bvec;
	sector_t sector;
	int i;
	int err = -EIO;

	sector = bio->bi_sector;
	if (sector + (bio->bi_size >> SECTOR_SHIFT) >
						get_capacity(bdev->bd_disk))
		goto out;

	rw = bio_rw(bio);
	if (rw == READA)
		rw = READ;

	bio_for_each_segment(bvec, bio, i) {
		unsigned int len = bvec->bv_len;
		err = brd_do_bvec(brd, bvec->bv_page, len,
					bvec->bv_offset, rw, sector);
		if (err)
			break;
		sector += len >> SECTOR_SHIFT;
	}

out:
	bio_endio(bio, err);

	return 0;
}

#ifdef CONFIG_BLK_DEV_XIP
static int brd_direct_access (struct block_device *bdev, sector_t sector,
			void **kaddr, unsigned long *pfn)
{
	struct brd_device *brd = bdev->bd_disk->private_data;
	struct page *page;

	if (!brd)
		return -ENODEV;
	if (sector & (PAGE_SECTORS-1))
		return -EINVAL;
	if (sector + PAGE_SECTORS > get_capacity(bdev->bd_disk))
		return -ERANGE;
	page = brd_insert_page(brd, sector);
	if (!page)
		return -ENOMEM;
	*kaddr = page_address(page);
	*pfn = page_to_pfn(page);

	return 0;
}
#endif

static int brd_ioctl(struct block_device *bdev, fmode_t mode,
			unsigned int cmd, unsigned long arg)
{
	struct brd_device *brd = bdev->bd_disk->private_data;
	int error;

	if (cmd == PCMDISK_CRASH) {
		list_for_each_entry(brd, &brd_devices, brd_list) {
			pcm_trigger_crash(brd->pcm);
		}	
		return 0;
	}

	if (cmd == PCMDISK_CRASH_RESET) {
		list_for_each_entry(brd, &brd_devices, brd_list) {
			pcm_trigger_crash_reset(brd->pcm);
		}	
		return 0;
	}

	if (cmd != BLKFLSBUF)
		return -ENOTTY;

	/*
	 * ram device BLKFLSBUF has special semantics, we want to actually
	 * release and destroy the pcmdisk data.
	 */
	mutex_lock(&bdev->bd_mutex);
	error = -EBUSY;
	if (bdev->bd_openers <= 1) {
		/*
		 * Invalidate the cache first, so it isn't written
		 * back to the device.
		 *
		 * Another thread might instantiate more buffercache here,
		 * but there is not much we can do to close that race.
		 */
		invalidate_bh_lrus();
		truncate_inode_pages(bdev->bd_inode->i_mapping, 0);
		brd_free_pages(brd);
		error = 0;
	}
	mutex_unlock(&bdev->bd_mutex);

	return error;
}

static const struct block_device_operations brd_fops = {
	.owner =		THIS_MODULE,
	.locked_ioctl =		brd_ioctl,
#ifdef CONFIG_BLK_DEV_XIP
	.direct_access =	brd_direct_access,
#endif
};

/*
 * And now the modules code and kernel interface.
 */
static int rd_nr;
int rd_size = CONFIG_BLK_DEV_PCM_SIZE;
static int max_part;
static int part_shift;
module_param(rd_nr, int, 0);
MODULE_PARM_DESC(rd_nr, "Maximum number of brd devices");
module_param(rd_size, int, 0);
MODULE_PARM_DESC(rd_size, "Size of each RAM disk in kbytes.");
module_param(max_part, int, 0);
MODULE_PARM_DESC(max_part, "Maximum number of partitions per RAM disk");
MODULE_LICENSE("GPL");
MODULE_ALIAS_BLOCKDEV_MAJOR(PCMDISK_MAJOR);
MODULE_ALIAS("rd");

#ifndef MODULE
/* Legacy boot options - nonmodular */
static int __init pcmdisk_size(char *str)
{
	rd_size = simple_strtol(str, NULL, 0);
	return 1;
}

static int __init pcmdisk_size2(char *str)
{
	return pcmdisk_size(str);
}
__setup("pcmdisk=", pcmdisk_size);
__setup("pcmdisk_size=", pcmdisk_size2);
#endif

/*
 * The device scheme is derived from loop.c. Keep them in synch where possible
 * (should share code eventually).
 */

static struct brd_device *brd_alloc(int i)
{
	struct brd_device *brd;
	struct gendisk *disk;

	brd = kzalloc(sizeof(*brd), GFP_KERNEL);
	if (!brd)
		goto out;
	brd->brd_number		= i;
	spin_lock_init(&brd->brd_lock);
	INIT_RADIX_TREE(&brd->brd_pages, GFP_ATOMIC);

	brd->brd_queue = blk_alloc_queue(GFP_KERNEL);
	if (!brd->brd_queue)
		goto out_free_dev;
	blk_queue_make_request(brd->brd_queue, brd_make_request);
	blk_queue_ordered(brd->brd_queue, QUEUE_ORDERED_TAG, NULL);
	blk_queue_max_sectors(brd->brd_queue, 1024);
	blk_queue_bounce_limit(brd->brd_queue, BLK_BOUNCE_ANY);

	disk = brd->brd_disk = alloc_disk(1 << part_shift);
	if (!disk)
		goto out_free_queue;
	disk->major		= PCMDISK_MAJOR;
	disk->first_minor	= i << part_shift;
	disk->fops		= &brd_fops;
	disk->private_data	= brd;
	disk->queue		= brd->brd_queue;
	disk->flags |= GENHD_FL_SUPPRESS_PARTITION_INFO;
	sprintf(disk->disk_name, "pcm%d", i);
	set_capacity(disk, rd_size * 2);

	/* Initialize PCM layer */
	pcm_init(&brd->pcm);

	return brd;

out_free_queue:
	blk_cleanup_queue(brd->brd_queue);
out_free_dev:
	kfree(brd);
out:
	return NULL;
}

static void brd_free(struct brd_device *brd)
{
	put_disk(brd->brd_disk);
	blk_cleanup_queue(brd->brd_queue);
	brd_free_pages(brd);
	pcm_fini(brd->pcm);
	kfree(brd);
}

static struct brd_device *brd_init_one(int i)
{
	struct brd_device *brd;

	list_for_each_entry(brd, &brd_devices, brd_list) {
		if (brd->brd_number == i)
			goto out;
	}

	brd = brd_alloc(i);
	if (brd) {
		add_disk(brd->brd_disk);
		list_add_tail(&brd->brd_list, &brd_devices);
	}
out:
	return brd;
}

static void brd_del_one(struct brd_device *brd)
{
	list_del(&brd->brd_list);
	del_gendisk(brd->brd_disk);
	brd_free(brd);
}

static struct kobject *brd_probe(dev_t dev, int *part, void *data)
{
	struct brd_device *brd;
	struct kobject *kobj;

	mutex_lock(&brd_devices_mutex);
	brd = brd_init_one(dev & MINORMASK);
	kobj = brd ? get_disk(brd->brd_disk) : ERR_PTR(-ENOMEM);
	mutex_unlock(&brd_devices_mutex);

	*part = 0;
	return kobj;
}


/* 
 * The function operations of the character device driver controlling the 
 * block device driver 
 */ 
static int pcmctrl_ioctl (struct inode *inode, struct file *filp,
                          unsigned int cmd, unsigned long arg) 
{
	struct brd_device *brd;

	if (cmd == PCMDISK_SET_PCM_BANDWIDTH) {
		PCM_BANDWIDTH_MB = arg;
		printk(KERN_INFO "SET PCM bandwidth: %lu\n", arg);
		return 0;
	}

	if (cmd == PCMDISK_GET_PCM_BANDWIDTH) {
		printk(KERN_INFO "GET PCM bandwidth: %d\n", PCM_BANDWIDTH_MB);
		return PCM_BANDWIDTH_MB;
	}

	if (cmd == PCMDISK_SET_DRAM_BANDWIDTH) {
		DRAM_BANDWIDTH_MB = arg;
		printk(KERN_INFO "SET DRAM bandwidth: %lu\n", arg);
		return 0;
	}

	if (cmd == PCMDISK_GET_DRAM_BANDWIDTH) {
		printk(KERN_INFO "GET DRAM bandwidth: %d\n", DRAM_BANDWIDTH_MB);
		return DRAM_BANDWIDTH_MB;
	}

	if (cmd == PCMDISK_RESET_STATISTICS) {
		list_for_each_entry(brd, &brd_devices, brd_list) {
			pcm_stat_reset(brd->pcm);
		}	

		return DRAM_BANDWIDTH_MB;
	}

	if (cmd == PCMDISK_PRINT_STATISTICS) {
		list_for_each_entry(brd, &brd_devices, brd_list) {
			pcm_stat_print(brd->pcm);
		}	
		return DRAM_BANDWIDTH_MB;
	}

	return 0;
}


struct file_operations pcmctrl_fops = {
	ioctl: pcmctrl_ioctl
};


static int __init brd_init(void)
{
	int i, nr;
	unsigned long range;
	struct brd_device *brd, *next;

	/*
	 * brd module now has a feature to instantiate underlying device
	 * structure on-demand, provided that there is an access dev node.
	 * However, this will not work well with user space tool that doesn't
	 * know about such "feature".  In order to not break any existing
	 * tool, we do the following:
	 *
	 * (1) if rd_nr is specified, create that many upfront, and this
	 *     also becomes a hard limit.
	 * (2) if rd_nr is not specified, create 1 rd device on module
	 *     load, user can further extend brd device by create dev node
	 *     themselves and have kernel automatically instantiate actual
	 *     device on-demand.
	 */

	part_shift = 0;
	if (max_part > 0)
		part_shift = fls(max_part);

	if (rd_nr > 1UL << (MINORBITS - part_shift))
		return -EINVAL;

	if (rd_nr) {
		nr = rd_nr;
		range = rd_nr;
	} else {
		nr = CONFIG_BLK_DEV_PCM_COUNT;
		range = 1UL << (MINORBITS - part_shift);
	}

	if (register_blkdev(PCMDISK_MAJOR, "pcmdisk"))
		return -EIO;

	if (register_chrdev(PCMDISK_CONTROL_MAJOR, "pcm-ctrl", &pcmctrl_fops)) {
		goto out_unregister_blockdev;
	}

	pcm_global_init();

	for (i = 0; i < nr; i++) {
		brd = brd_alloc(i);
		if (!brd)
			goto out_free;
		list_add_tail(&brd->brd_list, &brd_devices);
	}



	/* point of no return */

	list_for_each_entry(brd, &brd_devices, brd_list)
		add_disk(brd->brd_disk);

	blk_register_region(MKDEV(PCMDISK_MAJOR, 0), range,
				  THIS_MODULE, brd_probe, NULL, NULL);

	printk(KERN_INFO "brd: module loaded\n");
	return 0;

out_free:
	list_for_each_entry_safe(brd, next, &brd_devices, brd_list) {
		list_del(&brd->brd_list);
		brd_free(brd);
	}
	unregister_chrdev(PCMDISK_CONTROL_MAJOR, "pcm-ctrl");
out_unregister_blockdev:
	unregister_blkdev(PCMDISK_MAJOR, "pcmdisk");
	pcm_global_fini();

	return -ENOMEM;
}

static void __exit brd_exit(void)
{
	unsigned long range;
	struct brd_device *brd, *next;

	range = rd_nr ? rd_nr :  1UL << (MINORBITS - part_shift);

	list_for_each_entry_safe(brd, next, &brd_devices, brd_list) {
		brd_del_one(brd);
	}
	pcm_global_fini();
	unregister_chrdev(PCMDISK_CONTROL_MAJOR, "pcm-ctrl");
	blk_unregister_region(MKDEV(PCMDISK_MAJOR, 0), range);
	unregister_blkdev(PCMDISK_MAJOR, "pcmdisk");
}

module_init(brd_init);
module_exit(brd_exit);

