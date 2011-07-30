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


void nilfs_atime_fill_inode(struct inode *inode)
{
	/* XXX stub */
	inode->i_atime = inode->i_mtime;
}

void nilfs_atime_update_table(struct inode *inode)
{
	/* XXX stub */
}

/**
 * nilfs_atime_read - read or get atime node
 * @sb: super block instance
 * @raw_inode: on-disk atime file inode
 * @inodep: buffer to store the inode
 */
int nilfs_atime_read(struct super_block *sb, struct nilfs_inode *raw_inode,
		     struct inode **inodep)
{
	int err;
	struct inode *atime_ino;

	atime_ino = nilfs_iget_locked(sb, NULL, NILFS_CPFILE_INO);
	if (unlikely(!atime_ino))
		return -ENOMEM;
	if (!(atime_ino->i_state & I_NEW))
		goto out;
	
	err = nilfs_read_inode_common(atime_ino, raw_inode);
	if (err)
		goto failed;
	
	unlock_new_inode(atime_ino);
 out:
	*inodep = atime_ino;
	return 0;
 failed:
	iget_failed(atime_ino);
	return err;
}
