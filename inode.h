#ifndef _INODE_H
#define _INODE_H

cofs_inode_t *cofs_raw_inode(struct super_block *sb, unsigned long ino,
        struct buffer_head *bh);

struct inode *cofs_iget(struct super_block *sb, unsigned long ino);

void cofs_iput(struct inode *inode); 

struct inode *cofs_inode_alloc(struct super_block *sb, unsigned short int type);

void cofs_inode_evict(struct inode *inode);

#endif
