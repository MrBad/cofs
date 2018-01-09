#include <linux/buffer_head.h>
#include "cofs_common.h"
#include "block.h"

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
    pr_debug("cofs_iput: inode: %lu, mode: %u, ino mode: %u\n", 
            inode->i_ino, dino->type, inode->i_mode);
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
    inode->i_mode = dino->type;
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

static int cofs_truncate(struct inode *inode, unsigned int length)
{
    unsigned int fbn, fbs, fbe; // file block num, start, end
    unsigned int *blocks, sidx, didx, rel_b, pblock;
    struct buffer_head *buf, *dino_buf = NULL;
    struct super_block *sb = inode->i_sb;
    cofs_inode_t *dino;
    
    pr_debug("truncating inode %lu to %u length\n", inode->i_ino, length);
    if (length > inode->i_size) {
        return -1;
    }
    fbs = length / COFS_BLOCK_SIZE;
    fbe = (inode->i_size / COFS_BLOCK_SIZE) + 1;
    dino = cofs_raw_inode(inode->i_sb, inode->i_ino, dino_buf);

    for (fbn = fbs; fbn < fbe; fbn++) {
        if (fbn < NUM_DIRECT) {
            if (dino->addrs[fbn]) {
                cofs_block_free(sb, dino->addrs[fbn]);
                dino->addrs[fbn] = 0;
            }
        } else if (fbn < NUM_DIRECT + NUM_SIND) {
            buf = sb_bread(sb, dino->addrs[SIND_IDX]);
            blocks = (unsigned int *) buf->b_data;
            sidx = fbn - NUM_DIRECT;
            cofs_block_free(sb, blocks[sidx]);
            blocks[sidx] = 0;
            mark_buffer_dirty(buf);
            brelse(buf);
            if (cofs_scan_block(sb, dino->addrs[SIND_IDX]) == 0) {
                cofs_block_free(sb, dino->addrs[SIND_IDX]);
                dino->addrs[SIND_IDX] = 0;
            }
        } else if (fbn < MAX_FILE_SIZE) {
            rel_b = fbn - NUM_DIRECT - NUM_SIND;
            sidx = fbn / NUM_EINB;
            didx = fbn % NUM_EINB;

            buf = sb_bread(sb, dino->addrs[DIND_IDX]);
            blocks = (unsigned int *) buf->b_data;
            pblock = blocks[sidx];
            brelse(buf);
            
            buf = sb_bread(sb, pblock);
            blocks = (unsigned int *) buf->b_data;
            cofs_block_free(sb, blocks[didx]);
            blocks[didx] = 0;
            mark_buffer_dirty(buf);
            brelse(buf);

            if (cofs_scan_block(sb, pblock) == 0) {
                cofs_block_free(sb, pblock);
                buf = sb_bread(sb, dino->addrs[DIND_IDX]);
                blocks = (unsigned int *) buf->b_data;
                blocks[sidx] = 0;
                mark_buffer_dirty(buf);
                brelse(buf);
            }

            if (cofs_scan_block(sb, dino->addrs[DIND_IDX]) == 0) {
                cofs_block_free(sb, dino->addrs[DIND_IDX]);
                dino->addrs[DIND_IDX] = 0;
            }
        }
    }
    inode->i_size = length;
    cofs_iput(inode);
    return 0;
}

void cofs_inode_evict(struct inode *inode) 
{
    truncate_inode_pages_final(&inode->i_data);
    clear_inode(inode);
    pr_debug("cofs_inode_evict called for inode: %lu\n", inode->i_ino); 
    if (inode->i_nlink) {
        return;
    }
    // if this node has no more links to it, delete it //
    pr_debug("cofs_inode_evict: deleting from disk inode: %lu, size: %llu, links: %u\n",
            inode->i_ino, inode->i_size, inode->i_nlink);

    inode->i_mode = 0;
    cofs_truncate(inode, 0);
    //cofs_iput(inode);
}
