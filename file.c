#include <linux/module.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include "cofs_common.h"
#include "inode.h"
#include "block.h"

/**
 * Reads a file content into the buffer having max size, starting from offset
 *  We can only read one physical block at a time from hard drive, so we must
 *  ajust the max bytes to read.
 *  Returns the number of bytes read and updates the offset
 */
ssize_t cofs_file_read(struct file *file, char *buffer, size_t max, loff_t *offset)
{
    unsigned int block_no, num_bytes, total;
    struct buffer_head *bh;
    struct inode *inode = file_inode(file);

    // check if we are trying to read outside the file size limit //
    if (*offset > inode->i_size) {
        return 0;
    }
    // adjust the maximum number of bytes //
    if (*offset + max > inode->i_size) {
        max = inode->i_size - *offset;
    }
    
    for (total = 0; total < max; total += num_bytes) {
        block_no = cofs_get_real_block(inode, *offset / COFS_BLOCK_SIZE);
        bh = sb_bread(inode->i_sb, block_no);
        num_bytes = cofs_min(max - total, COFS_BLOCK_SIZE - *offset % COFS_BLOCK_SIZE);
        memcpy(buffer, bh->b_data + *offset % COFS_BLOCK_SIZE, num_bytes);
        *offset += num_bytes;
        buffer += num_bytes; // use it only once
        brelse(bh);
    }
    
    return total;
}

ssize_t cofs_file_write(struct file *file, const char *buffer, size_t max, loff_t *offset)
{
    unsigned int block_no, total, num_bytes;
    struct buffer_head *bh;
    struct inode *inode = file_inode(file);
    
    printk("cofs_file_write - inode: %lu, offset: %llu, max: %lu\n", 
            inode->i_ino, *offset, max);
   
    if (*offset > inode->i_size) {
        pr_debug("writing at offset > size... should be allowed?!\n");
        return 0;
    }
    if (*offset + max > MAX_FILE_SIZE * COFS_BLOCK_SIZE) {
        return -1;
    }
    for (total = 0; total < max; total += num_bytes) {
        if(!(block_no = cofs_get_real_block(inode, *offset / COFS_BLOCK_SIZE))) {
            pr_debug("cofs_get_real_block returned 0\n");
            return -1;
        }
        printk("cofs-write inode: %lu, relative_block: %llu, block_no:%u\n", 
                inode->i_ino, *offset/COFS_BLOCK_SIZE, block_no);
        num_bytes = cofs_min(max - total, COFS_BLOCK_SIZE - *offset % COFS_BLOCK_SIZE);
        bh = sb_bread(inode->i_sb, block_no);
        memcpy(bh->b_data + *offset % COFS_BLOCK_SIZE, buffer, num_bytes);
        *offset += num_bytes;
        buffer += num_bytes;
        mark_buffer_dirty(bh);
        brelse(bh);
    }
    if (max > 0 && *offset > inode->i_size) {
        printk("Update inode size: inode: %lu, size: %llu, new_size: %llu\n",
                inode->i_ino, inode->i_size, *offset);
        inode->i_size = *offset;
        cofs_iput(inode);
    }
    return total;
}

struct inode_operations cofs_file_inode_ops = {
	.getattr        = simple_getattr,
};

struct file_operations cofs_file_operations = {
	.read           = cofs_file_read,
	.write          = cofs_file_write,
	.mmap           = generic_file_mmap,
	.fsync          = noop_fsync,
	.llseek         = generic_file_llseek,
};

