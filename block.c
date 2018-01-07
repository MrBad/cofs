#include <linux/buffer_head.h>
#include "cofs_common.h"
#include "inode.h"

/*
 * Zeroes a phisical block on disk
 */
static void cofs_block_bzero(struct super_block *sb, unsigned int block_no)
{
    struct buffer_head *bh = sb_bread(sb, block_no);
    memset(bh->b_data, 0, sizeof(bh->b_size));
    mark_buffer_dirty(bh);
    brelse(bh);
}

/**
 * Finds a free block of COFS_BLOCK_SIZE on disk
 * marks it as active and returns it's physical address
 * On failure, returns 0, which is not a valid block
 */
unsigned int cofs_block_alloc(struct super_block *sb)
{
    struct buffer_head *bh;
    unsigned int block, scan, idx, mask, iter = 0;
    cofs_superblock_t *cofs_sb = (cofs_superblock_t *) sb->s_fs_info;
    for (block = 0; block < cofs_sb->size; block += BITS_PER_BLOCK) {
        bh = sb_bread(sb, BITMAP_BLOCK(block, cofs_sb));
        for (scan = 0; scan < COFS_BLOCK_SIZE / sizeof(int); scan++, iter++) {
            if (((unsigned int *) bh->b_data)[scan] != 0xFFFFFFFF) {
                break;
            }
        }
        if (scan != COFS_BLOCK_SIZE / sizeof(int)) {
            for (idx = scan * 32; idx < (scan + 1)*32; idx++, iter++) {
                mask = 1 << (idx % 8);
                if((bh->b_data[idx / 8] & mask) == 0) {
                    bh->b_data[idx / 8] |= mask;
                    mark_buffer_dirty(bh);
                    brelse(bh);
                    cofs_block_bzero(sb, block + idx);
                    return block + idx;
                }
            }
        }
        brelse(bh);
    }
    printk("Cannot find any free block\n");
    return 0;
}

/**
 * We cannot use iput here, because it does not handle the addrs
 */
unsigned int cofs_get_real_block(struct inode *inode, unsigned int ino_block)
{
    struct super_block *sb = inode->i_sb;
    struct buffer_head *ino_buf = NULL, // buffer to hold the inode
                       *buf;            // generic buffer for other manipulations
 
    cofs_inode_t *dino = cofs_raw_inode(sb, inode->i_ino, ino_buf);
    unsigned int block_no = 0, // block alocated or 0 on error
                 sidx,  // single indirect index 
                 //didx,  // double indirect index
                 *blocks;

    if (ino_block < NUM_DIRECT) { 
        if (dino->addrs[ino_block] == 0) {
            dino->addrs[ino_block] = cofs_block_alloc(sb); // alocate direct data block
            //mark_buffer_dirty(ino_buf);           // dino is a pointer in ino_block data ;)
            printk("alocated block no: %d\n", block_no);
        }
        block_no = dino->addrs[ino_block];
    } 
    else if (ino_block < NUM_DIRECT + NUM_SIND) {
        if (dino->addrs[SIND_IDX] == 0) {
            dino->addrs[SIND_IDX] = cofs_block_alloc(sb); // alocate block for indirect table
            printk("allocating indirect table block\n");
            //mark_buffer_dirty(ino_buf);
        }
        buf = sb_bread(sb, dino->addrs[SIND_IDX]);       // load indirect table
        blocks = (unsigned int *) buf->b_data;
        sidx = ino_block - NUM_DIRECT;
        if (blocks[sidx] == 0) {
            blocks[sidx] = cofs_block_alloc(sb);     // alocate block for data
            //mark_buffer_dirty(buf);
        }
        block_no = blocks[sidx];
        brelse(buf);
    }
    else if (ino_block < MAX_FILE_SIZE) {
        printk("Double indirect not available");
    }
    else {
        printk("Requested relative inode block out of MAX_FILE_SIZE %u\n", ino_block);
    }
    
    brelse(ino_buf);
    //printk("cofs_get_real_block: inode: %lu, relative: %u, block_no: %u\n", 
    //       inode->i_ino, ino_block, block_no);
    return block_no;
}
