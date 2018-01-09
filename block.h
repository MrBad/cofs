#ifndef _BLOCK_H
#define _BLOCK_H

/**
 * Convert from inode relative block number (like 0, 1, 2..), to physical
 * disk block number.
 */
unsigned int cofs_get_real_block(struct inode *inode, unsigned int ino_block);
int cofs_block_free(struct super_block *sb, unsigned int block);
int cofs_scan_block(struct super_block *sb, unsigned int block);

#endif
