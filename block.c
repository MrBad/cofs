/**
 *  Blocks, rocks and the bits
 *
 *  This file will handle the low level allocation of blocks.
 *  A block is a group of bytes on disk and right now it has a length of 512,
 *  defined by COFS_BLOCK_SIZE
 *
 */
#include <linux/buffer_head.h>
#include "cofs_common.h"
#include "inode.h"

/*
 * Zero/erase a physical block on disk
 */
static void cofs_block_bzero(struct super_block *sb, unsigned int block_no)
{
    struct buffer_head *bh = sb_bread(sb, block_no);
    memset(bh->b_data, 0, sizeof(bh->b_size));
    mark_buffer_dirty(bh);
    brelse(bh);
}

/**
 * Finds a free block on disk
 * marks it as active and returns it's physical address
 * On failure, returns 0, which is not a valid block
 */
static unsigned int cofs_block_alloc(struct super_block *sb)
{
    struct buffer_head *bh;
    unsigned int block, scan, idx, mask;
    cofs_superblock_t *cofs_sb = (cofs_superblock_t *) sb->s_fs_info;
    for (block = 0; block < cofs_sb->size; block += BITS_PER_BLOCK) {
        bh = sb_bread(sb, BITMAP_BLOCK(block, cofs_sb));
        for (scan = 0; scan < COFS_BLOCK_SIZE / sizeof(int); scan++) {
            if (((unsigned int *) bh->b_data)[scan] != 0xFFFFFFFF) {
                break;
            }
        }
        if (scan != COFS_BLOCK_SIZE / sizeof(int)) {
            for (idx = scan * 32; idx < (scan + 1) * 32; idx++) {
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
    printk("Cannot find any free block, out of space?!\n");
    return 0;
}

int cofs_block_free(struct super_block *sb, unsigned int block)
{
    struct buffer_head *bh;
    unsigned int bitmap_block, idx, mask;
    bitmap_block = BITMAP_BLOCK(block, ((cofs_superblock_t *) sb->s_fs_info));
    bh = sb_bread(sb, bitmap_block);
    idx = block % BITS_PER_BLOCK;
    mask = 1 << (idx % 8);
    if((bh->b_data[idx / 8] & mask) == 0) {
        pr_err("Block %u allready free", block);
        return -1;
    }
    pr_debug("Freeing block %u\n", block);
    bh->b_data[idx / 8] &= ~mask;
    mark_buffer_dirty(bh);
    brelse(bh);
    return 0;
}

int cofs_scan_block(struct super_block *sb, unsigned int block) 
{
    struct buffer_head *bh;
    unsigned int scan, i = 0;
    bh = sb_bread(sb, block);
    for(scan = 0; scan < BLOCK_SIZE / sizeof(int); scan++) {
        if(((uint32_t *)bh->b_data)[scan] != 0) {
            i++;
        }
    }
    brelse(bh);

    return i;
}

/**
 * Returning the real disk block number, by giving relative block of inode.
 * Eg. block 1 of inode, that represents bytes from 512-1024 will be 
 * mapped to disk block 3059 (supposing).
 * If we try to write ouside, in an analocated block, a new free block will
 * be mapped in. Usually the read functions will not read ouside the inode size.
 * Maybe a new guard will be added - like bool extend, and only extending on 
 * write functions.
 * We will not flush the inode buffer, because on some linux errors when 
 * marking it dirty, twice. The function that calls this one and modify the inode
 * will be the write function, which also alter and mark the inode buffer as dirty
 */
unsigned int cofs_get_real_block(struct inode *inode, unsigned int ino_block)
{
    struct super_block *sb = inode->i_sb;
    struct buffer_head *ino_buf = NULL, // buffer to hold the inode
                       *buf;            // generic buffer for other manipulations
 
    cofs_inode_t *dino = cofs_raw_inode(sb, inode->i_ino, ino_buf);
    unsigned int block_no = 0,  // block alocated or 0 on error
                 rel_b,         // relative block number inside a table
                 pblock,
                 sidx,          // single indirect index 
                 didx,          // double indirect index
                 *blocks;

    // direct alocation //
    if (ino_block < NUM_DIRECT) { 
        if (dino->addrs[ino_block] == 0) {
            // alocate direct data block
            dino->addrs[ino_block] = cofs_block_alloc(sb); 
        }
        block_no = dino->addrs[ino_block];
    } 
    // single indirect allocation //
    else if (ino_block < NUM_DIRECT + NUM_SIND) {
        if (dino->addrs[SIND_IDX] == 0) {
            // alocate block for indirect table
            dino->addrs[SIND_IDX] = cofs_block_alloc(sb); 
        }
        buf = sb_bread(sb, dino->addrs[SIND_IDX]); // load indirect table
        blocks = (unsigned int *) buf->b_data;
        sidx = ino_block - NUM_DIRECT;
        if (blocks[sidx] == 0) {
            // alocate block for data
            blocks[sidx] = cofs_block_alloc(sb);
            mark_buffer_dirty(buf);
        }
        block_no = blocks[sidx];
        brelse(buf);
    }
    // double indirect allocation //
    else if (ino_block < MAX_FILE_SIZE) {
        rel_b = ino_block - NUM_DIRECT - NUM_SIND; // block relative number to this zone
        // index into the first level table //
        sidx = ino_block / NUM_EINB;
        // index into the second level table //
        didx = ino_block % NUM_EINB;

        if (dino->addrs[DIND_IDX] == 0) {
            // allocating a block for primary indirect table //
            dino->addrs[DIND_IDX] = cofs_block_alloc(sb);
        }
        buf = sb_bread(sb, dino->addrs[DIND_IDX]);
        blocks = (unsigned int *) buf->b_data;
        if (blocks[sidx] == 0) {
            // allocating a block for secondary indirect table //
            blocks[sidx] = cofs_block_alloc(sb);
            mark_buffer_dirty(buf);
        }
        pblock = blocks[sidx];
        brelse(buf);

        buf = sb_bread(sb, pblock);
        blocks = (unsigned int *) buf->b_data;
        if (blocks[didx] == 0) {
            // finally alocating the data block //
            blocks[didx] = cofs_block_alloc(sb);
            mark_buffer_dirty(buf);
        }
        block_no = blocks[didx];
        brelse(buf);
    }
    else {
        pr_err("Inode's relative block is out of MAX_FILE_SIZE - block: %u, max: %lu\n", ino_block, MAX_FILE_SIZE);
    }
    
    brelse(ino_buf);
    return block_no;
}
