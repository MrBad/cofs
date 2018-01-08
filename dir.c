#include <linux/buffer_head.h>
#include <linux/fs.h>
#include "cofs_common.h"
#include "inode.h"
#include "block.h"

static int cofs_readdir(struct file *file, struct dir_context *ctx)
{
    struct buffer_head *bh = NULL;
    struct inode *inode = file_inode(file);
    unsigned int block_no;
    int offs, total;
    struct cofs_dirent *cdir;
    
    while (ctx->pos < inode->i_size) {
        block_no = cofs_get_real_block(inode, ctx->pos / COFS_BLOCK_SIZE);
        offs = ctx->pos % COFS_BLOCK_SIZE;
        bh = sb_bread(inode->i_sb, block_no);
        total = 0;
        do {
            cdir = (struct cofs_dirent *) (bh->b_data + offs);
            if (cdir->d_ino) {
                dir_emit(ctx, cdir->d_name, COFS_FILE_NAME_MAX_LEN, cdir->d_ino, 
                        DT_UNKNOWN);
            }
            offs += sizeof(*cdir);
            total += sizeof(*cdir);
        } while (offs < COFS_BLOCK_SIZE);

        ctx->pos += total;
        brelse(bh);
    }

    return 0;
}

/**
 * This file is called when kernel is resolving a path. dir is the inode of the parent
 * It is querying the parent inode and check for the file name in dentry.
 * If it founds one, it populates it's inode number
 */
struct dentry *cofs_lookup(struct inode *dir, struct dentry *dentry, 
        unsigned int what)
{
    struct buffer_head *bh;
    unsigned int num_blocks, block, block_no;
    struct inode *file_inode;
    struct cofs_dirent *cdir;
    
    num_blocks = dir->i_size / COFS_BLOCK_SIZE;
    for (block = 0; block < num_blocks; block++) {
        if (!(block_no = cofs_get_real_block(dir, block))) {
            pr_err("cofs_lookup: invalid block %u, inode: %lu\n", block, dir->i_ino);
            return NULL;
        }
        bh = sb_bread(dir->i_sb, block_no);
        cdir = (struct cofs_dirent *) bh->b_data;
        while (cdir < (struct cofs_dirent *) (bh->b_data + COFS_BLOCK_SIZE)) {
            if(cdir->d_ino != 0) {
                if(!strncmp(cdir->d_name, dentry->d_name.name, COFS_FILE_NAME_MAX_LEN)) {
                    file_inode = cofs_iget(dir->i_sb, cdir->d_ino);
                    d_add(dentry, file_inode);
                    return NULL;
                }
            }
            cdir++;
        }
        brelse(bh);
    }
    return NULL;
}

/**
 * Adds an entry into parent inode to this inode, with name 
 * The function do not check if this inode number is already linked, that's
 * the responsability of the caller
 */
static int cofs_dir_link(struct inode *dir, unsigned int ino, const char *name)
{
    struct buffer_head *bh;
    unsigned int num_blocks,    // total number of blocks this file has
                 block,         // used for iteration
                 block_no;      // physical block number (on disk)
    struct cofs_dirent *cdir;
    
    pr_debug("cofs_dir_link: linking inode %u, name %s, to it's parent %lu\n", 
            ino, name, dir->i_ino);

    num_blocks = dir->i_size / COFS_BLOCK_SIZE;

    // yes, block <= num_blocks. 
    // If we pass the boundary, a new block will be allocated //
    for (block = 0; block <= num_blocks; block++) {
        if (!(block_no = cofs_get_real_block(dir, block))) {
            printk("cofs_dir_link: invalid block for %s, block: %u", name, block_no);
            return -1;
        }
        bh = sb_bread(dir->i_sb, block_no);
        cdir = (struct cofs_dirent *) bh->b_data;
        while(cdir < (struct cofs_dirent *) (bh->b_data + COFS_BLOCK_SIZE)) {
            if (cdir->d_ino == 0) {
                cdir->d_ino = ino;
                strncpy(cdir->d_name, name, COFS_FILE_NAME_MAX_LEN);
                mark_buffer_dirty(bh);
                brelse(bh);
                // if is a newly allocated buffer, update it's size
                if (block == num_blocks) {
                    pr_debug("cofs_dir_link: a new block was alocated: %u\n", block_no);
                    dir->i_size += COFS_BLOCK_SIZE;
                }
                inc_nlink(dir);
                pr_debug("cofs_dir_link: inode: %lu, no links: %u\n", 
                        dir->i_ino, dir->i_nlink);
                cofs_iput(dir);
                return 0;
            }
            cdir++;
        }
        brelse(bh);
    }
    return -1;
}

static int cofs_mknod(struct inode *dir, struct dentry *dentry, umode_t mode, dev_t dev)
{
    unsigned int m = mode & S_IFMT;
    struct inode *inode;

    inode = cofs_inode_alloc(dir->i_sb, m);
    inode->i_mode = mode;
    set_nlink(inode, 1);
    d_add(dentry, inode); // do we need this?
    if (m & S_IFDIR) {
        cofs_dir_link(inode, inode->i_ino, ".");    // add an entry to itself
        cofs_dir_link(inode, dir->i_ino, "..");     // add an entry to it's parent
    }
    cofs_dir_link(dir, inode->i_ino, dentry->d_name.name); // self link to parent

    pr_debug("cofs: mknod %s, mode: %d\n", dentry->d_name.name, mode);
    return 0;
}

static int cofs_mkdir(struct inode * dir, struct dentry * dentry, umode_t mode)
{
    pr_debug("cofs: cofs_mkdir\n");
    return cofs_mknod(dir, dentry, mode | S_IFDIR, 0);
    return 0;
}


static int cofs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool b)
{
    pr_debug("cofs: cofs_create %s\n", dentry->d_name.name);
    return cofs_mknod(dir, dentry, mode | S_IFREG, 0);
}

int cofs_symlink(struct inode * dir, struct dentry *dentry, const char * symname)
{
    pr_debug("cofs: cofs_symlink\n");
    return -ENOSPC;
}

struct inode_operations cofs_dir_inode_ops = {
    .lookup         = cofs_lookup,
    //.link           = simple_link,
    //.unlink         = simple_unlink,
    //.symlink        = cofs_symlink,
    .mknod          = cofs_mknod,
    .mkdir          = cofs_mkdir,
    .create         = cofs_create,
    //.rmdir          = simple_rmdir,
    //.rename         = simple_rename,
};

struct file_operations cofs_dir_operations = {
    .llseek     = generic_file_llseek,
    //.read       = generic_read_dir,
    .iterate    = cofs_readdir,
    .fsync		= generic_file_fsync
};

