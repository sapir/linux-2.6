/*
 * atime.c - NILFS atime file.
 *
 * Copyright (C) 2006-2008 Nippon Telegraph and Telephone Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Written by Yehoshua Sapir <yasapir@gmail.com>
 */

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/nilfs2_fs.h>
#include "nilfs.h"
#include "mdt.h"

/*----------------------------------------------------------------------------*/
/* Structs of blocks in atime file                                            */
/*----------------------------------------------------------------------------*/

#pragma pack(1)
struct nilfs_atime_block_header {
    __u64 count;
};

/* XXX is block size 512 or 4096? */
#define NILFS_ENTRIES_IN_ATIME_BLOCK ((512 - sizeof(struct nilfs_atime_block_header)) / sizeof(struct timespec))

struct nilfs_atime_block {
    struct nilfs_atime_block_header header;
    struct timespec entries[NILFS_ENTRIES_IN_ATIME_BLOCK];
};
#pragma pack()

/*----------------------------------------------------------------------------*/
/* Functions for getting a mapped block to atime file                         */
/*----------------------------------------------------------------------------*/

static void nilfs_atime_block_init(struct inode *atime,
                                   struct buffer_head *bh,
                                   void *kaddr)
{
	struct nilfs_atime_block * atime_block = kaddr + bh_offset(bh);
    memset(atime_block, 0, sizeof(*atime_block));
}

static inline unsigned long nilfs_atime_get_block_number(unsigned long inode_num)
{
    return inode_num / NILFS_ENTRIES_IN_ATIME_BLOCK;
}

static inline struct timespec * nilfs_atime_get_entry_in_block(struct nilfs_atime_block * atime_block,
                                                              unsigned long inode_num)
{
    return &atime_block->entries[inode_num % NILFS_ENTRIES_IN_ATIME_BLOCK];
}

/**
 * nilfs_atime_get_block - get a block
 * @atime: inode of atime file
 * @inode_num: inode number
 * @block_out: pointer to the block
 * @block_bh_out: pointer to a buffer head
 *
 * Description: nilfs_atime_get_block() acquires the block hosting the atime of
 * @inode_num. A new block is created if doesn't exist.
 *
 * Return Value: On success, 0 is returned, and the block and the
 * buffer head of the block on which the block is located are stored in
 * the place pointed by @block_out and @block_bh_out, respectively. On error, one of the
 * following negative error codes is returned.
 *
 * %-EIO - I/O error.
 *
 * %-ENOMEM - Insufficient amount of memory available.
 */
static int nilfs_atime_get_block(struct inode * atimefile,
                                 __u64 inode_num,
                                 struct nilfs_atime_block ** block_out,
                                 struct buffer_head ** block_bh_out)
{
	struct buffer_head *block_bh;
	struct nilfs_atime_block * atime_block;
	void *kaddr;
	int ret;

	down_write(&NILFS_MDT(atimefile)->mi_sem);

    /* Get the block from MDT */
    ret = nilfs_mdt_get_block(atimefile,
                              nilfs_atime_get_block_number(inode_num),
                              1, nilfs_atime_block_init, &block_bh);
    if (ret < 0) {
		goto out;
    }

    /* Map the block to lower memory */
    kaddr = kmap(block_bh->b_page);
    atime_block = (struct nilfs_atime_block *)(kaddr + bh_offset(block_bh));

    /* Make sure the block will be flushed when we finish */
    mark_buffer_dirty(block_bh);
    nilfs_mdt_mark_dirty(atimefile);

    /* Return the atime_block and buffer head to the user */
    if (block_out != NULL) {
		*block_out = atime_block;
    }
    *block_bh_out = block_bh;

 out:
	up_write(&NILFS_MDT(atimefile)->mi_sem);
	return ret;
}

/**
 * nilfs_atime_put_block - put an atime block
 * @bh: buffer head returned by a successful call to nilfs_atime_get_block
 */
static void nilfs_atime_put_block(struct buffer_head * block_bh)
{
	kunmap(block_bh->b_page);
	brelse(block_bh);
}

/*----------------------------------------------------------------------------*/
/* External interfaces for setting/getting/deleting atime                     */
/*----------------------------------------------------------------------------*/

int nilfs_atime_fill_inode(struct inode * atimefile, struct inode * inode)
{
    int err;
    struct nilfs_atime_block * atime_block;
    struct buffer_head * block_bh;
    struct timespec * atime;

    if ((NULL == atimefile) || (atimefile == inode)) {
        inode->i_atime = inode->i_mtime;
        return 0;
    }

    err = nilfs_atime_get_block(atimefile, inode->i_ino, &atime_block, &block_bh);
    if (err < 0) {
        return err;
    }

    atime = nilfs_atime_get_entry_in_block(atime_block, inode->i_ino);
    if ((0 == atime->tv_sec) && (0 == atime->tv_nsec)) {
        /* time is invalid, set to valid time and increase count */
        *atime = inode->i_mtime;
        ++atime_block->header.count;
    }

    inode->i_atime = *atime;
    nilfs_atime_put_block(block_bh);
    return 0;
}

int nilfs_atime_update_from_inode(struct inode * atimefile, struct inode * inode)
{
    int err;
    struct nilfs_atime_block * atime_block;
    struct buffer_head * block_bh;
    struct timespec * atime;

    err = nilfs_atime_get_block(atimefile, inode->i_ino, &atime_block, &block_bh);
    if (err < 0) {
        return err;
    }

    atime = nilfs_atime_get_entry_in_block(atime_block, inode->i_ino);
    *atime = inode->i_atime;

    nilfs_atime_put_block(block_bh);
    return 0;
}

int nilfs_atime_delete_inode_entry(struct inode * atimefile, struct inode * inode)
{
    int err;
    int needs_release;
    struct nilfs_atime_block * atime_block;
    struct buffer_head * block_bh;
    struct timespec * atime;

    err = nilfs_atime_get_block(atimefile, inode->i_ino, &atime_block, &block_bh);
    if (err < 0) {
        return err;
    }

    atime = nilfs_atime_get_entry_in_block(atime_block, inode->i_ino);
    atime->tv_sec = 0;
    atime->tv_nsec = 0;
    --atime_block->header.count;
    needs_release = (0 == atime_block->header.count);

    nilfs_atime_put_block(block_bh);

    if (needs_release) {
        nilfs_mdt_delete_block(atimefile, nilfs_atime_get_block_number(inode->i_ino));
    }

    return 0;
}

int nilfs_atime_read(struct super_block *sb,
                     struct nilfs_inode *raw_inode,
                     struct inode **inodep)
{
	struct inode *atimefile;
	int err;

	atimefile = nilfs_iget_locked(sb, NULL, NILFS_ATIME_INO);
	if (unlikely(!atimefile))
		return -ENOMEM;
	if (!(atimefile->i_state & I_NEW))
		goto out;

	err = nilfs_mdt_init(atimefile, NILFS_MDT_GFP, 0);
	if (err)
		goto failed;

	nilfs_mdt_set_entry_size(atimefile,
                             sizeof(struct nilfs_atime_block),
                             sizeof(struct nilfs_atime_block_header));

	err = nilfs_read_inode_common(atimefile, raw_inode);
	if (err)
		goto failed;

	unlock_new_inode(atimefile);
 out:
	*inodep = atimefile;
	return 0;
 failed:
	iget_failed(atimefile);
	return err;
}
