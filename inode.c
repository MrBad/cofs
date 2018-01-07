#include <linux/buffer_head.h>
#include "cofs_common.h"

extern struct inode_operations cofs_dir_inode_ops;
extern struct file_operations cofs_dir_operations;
extern struct inode_operations cofs_file_inode_ops;
extern struct file_operations cofs_file_operations;

/**
 * Reads physical inode ino from disk, save the buffer into bh
 * and returns a pointer in this buffer to the inode
 * It is the caller duty to brelse this buffer
 * It can return NULL if cannot read this block number
 */
cofs_inode_t *cofs_raw_inode(struct super_block *sb, unsigned long ino, 
              struct buffer_head *bh)
{
    unsigned int block_no; 
    cofs_inode_t *dino = NULL;
    block_no = ((cofs_superblock_t *)sb->s_fs_info)->inode_start;
    block_no += ino / NUM_INOPB;
    if (!(bh = sb_bread(sb, block_no))) {
        return NULL;
    }

    dino = (cofs_inode_t *) bh->b_data;
    dino += (ino % NUM_INOPB);
    
    return dino;
}

/**
 * Puts back to disk the inode / updates it
 */
void cofs_iput(struct inode *inode) 
{
    cofs_inode_t *dino; // the disk inode
    struct buffer_head *bh;
    // cofs superblock //
    cofs_superblock_t *cofs_sb = (cofs_superblock_t *) inode->i_sb->s_fs_info;
    // block containing this inode //
    unsigned int block_no = (inode->i_ino) / NUM_INOPB + cofs_sb->inode_start;
    
    // read the buffer containing this disk inode
    bh = sb_bread(inode->i_sb, block_no);
    dino = (cofs_inode_t *) bh->b_data + inode->i_ino % NUM_INOPB;
    dino->type = inode->i_mode & S_IFMT; // not very sure...
    dino->uid = inode->i_uid.val;
    dino->gid = inode->i_gid.val;
    dino->num_links = inode->i_nlink;
    dino->atime = inode->i_atime.tv_sec;
    dino->ctime = inode->i_ctime.tv_sec;
    dino->mtime = inode->i_mtime.tv_sec;
    dino->size = inode->i_size;
    //--- addrs ? ---//
    mark_buffer_dirty(bh);
    brelse(bh);
}

struct inode *cofs_iget(struct super_block *sb, unsigned long ino)
{
    struct buffer_head *bh = NULL;  // where to read data //
    struct inode *inode;            // linux inode return //
    cofs_inode_t *dino;             // disk / raw inode
    inode = iget_locked(sb, ino);   // aquire inode

    if(!inode) 
        return ERR_PTR(-ENOMEM);

    // if is cached, return //
    if (!(inode->i_state & I_NEW))
        return inode;
    
    if (!(dino = cofs_raw_inode(sb, ino, bh))) {
        iget_failed(inode);
        return ERR_PTR(-EIO);
    }

    pr_debug("cofs: iget: %lu, size: %d, type: %d, links: %d\n",
            ino, dino->size, dino->type, dino->num_links);
    
    // assign to inode from raw //
    inode->i_mode = dino->type | 0555;
    inode->i_size = dino->size;
    i_uid_write(inode, dino->uid);
	i_gid_write(inode, dino->gid);
	set_nlink(inode, dino->num_links);

    switch (inode->i_mode & S_IFMT) {
        case S_IFDIR:
            pr_debug("cofs: inode %lu describe a directory\n", ino);
            inode->i_op = &cofs_dir_inode_ops;
            inode->i_fop = &cofs_dir_operations;
            break;

        case S_IFREG:
            pr_debug("cofs: inode %lu describe a regular file\n", ino);
            inode->i_op = &cofs_file_inode_ops;
            inode->i_fop = &cofs_file_operations;
            break;
            
        case S_IFLNK:
            pr_debug("cofs: inode %lu describe a link\n", ino);
            break;

        default:
            pr_warn("cofs: unknown inode %lu with mode: %o. Is a special_inode?\n", ino, inode->i_mode);
    }
    
    brelse(bh);
    unlock_new_inode(inode);
    
    return inode;
}

// Allocates a free inode on disk //
struct inode *cofs_inode_alloc(struct super_block *sb, unsigned short int type)
{
    struct buffer_head *bh;
    cofs_inode_t *dino;
    cofs_superblock_t *cofs_sb = (cofs_superblock_t *) sb->s_fs_info;
 
    // Slow thing. TODO - use an inode map on disk, like the bit block //
    unsigned int block, i;
    for (block = 0; block < cofs_sb->num_inodes / NUM_INOPB; block++)
    {
        bh = sb_bread(sb, cofs_sb->inode_start + block);
        dino = (cofs_inode_t *) bh->b_data;
        for (i = 0; i < NUM_INOPB; i++, dino++) {
            if (block == 0 && i == 0)
                continue;
            if (dino->type == 0) {
                memset(dino, 0, sizeof(*dino));
                dino->type = type;
                mark_buffer_dirty(bh);
                brelse(bh);
                printk("COFS: allocating inode: %lu\n", block * NUM_INOPB + i);
                return cofs_iget(sb, block * NUM_INOPB + i);
            }
        }
        brelse(bh);
    }
    pr_debug("cofs: inode_alloc - no free inodes!\n");
    return NULL;
}
