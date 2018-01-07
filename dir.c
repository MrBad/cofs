#include <linux/buffer_head.h>
#include <linux/fs.h>
#include "cofs_common.h"
#include "inode.h"
#include "block.h"

static int cofs_readdir(struct file *file, struct dir_context *ctx)
{
    struct buffer_head *bh = NULL;
    struct inode *inode = file_inode(file);
    unsigned long int block_no;
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
                dir_emit(ctx, cdir->d_name, COFS_FILE_MAX_LEN, cdir->d_ino, 
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
    struct inode *file_inode;
    unsigned long int block_no;
    struct buffer_head *bh = NULL;
    int pos = 0, total = 0;
    struct cofs_dirent *cdir;
    
    //pr_debug("cofs_lookup for %s\n", dentry->d_name.name);

    while (total <= dir->i_size) {
        block_no = cofs_get_real_block(dir, total / COFS_BLOCK_SIZE);
        bh = sb_bread(dir->i_sb, block_no);
        if(!bh) {
            pr_debug("sb_bread returned null in cofs_lookup, block_no: %lu\n", block_no);
            return NULL;
        }
        for (pos = 0; pos < COFS_BLOCK_SIZE; pos += sizeof(*cdir)) {
            cdir = (struct cofs_dirent *) (bh->b_data + pos);
            if (! cdir->d_ino)
                continue;
            if (strcmp(cdir->d_name, dentry->d_name.name) == 0) {
                pr_debug("found %s, inode: %d\n", cdir->d_name, cdir->d_ino);
                file_inode = cofs_iget(dir->i_sb, cdir->d_ino);
                d_add(dentry, file_inode);
                return NULL;
            }
        }
        total += pos;
        brelse(bh);
    }

    return NULL;
}

/**
 * Add an entry into parent inode to this inode, with name 
 * The function do not check if this inode number is already linked!!!
 * TODO: increment the number of links in dir / parent
 */
static int cofs_dir_link(struct inode *dir, unsigned int ino, const char *name)
{
    struct buffer_head *bh;
    unsigned int num_blocks,    // total number of blocks this file has
                 block,         // used for iteration
                 block_no;      // phisical on disk block number
    struct cofs_dirent *cdir;
    
    pr_debug("cofs_dir_link: linking inode: %u to it's parent %lu, with name %s\n", 
            ino, dir->i_ino, name);

    // first, search for a free entry in existing dir (which is a file :P) //
    num_blocks = dir->i_size / COFS_BLOCK_SIZE;
    printk("ino: %lu, size: %llu, num_blocks: %u\n",
            dir->i_ino, dir->i_size, num_blocks);
    for (block = 0; block < num_blocks; block++) {
        block_no = cofs_get_real_block(dir, block);
        bh = sb_bread(dir->i_sb, block_no);
        cdir = (struct cofs_dirent *) bh->b_data;
        while(cdir < (struct cofs_dirent *) (bh->b_data + COFS_BLOCK_SIZE)) {
            if (cdir->d_ino == 0) {
                pr_debug("cofs_link - we found an empty slot\n");
                cdir->d_ino = ino;
                strncpy(cdir->d_name, name, COFS_FILE_MAX_LEN);
                mark_buffer_dirty(bh);
                brelse(bh);
                return 0;
            }
            cdir++;
        }
        brelse(bh);
    }
    printk("dir_link: CANNOT FIND AN EMPTY SLOT\n"); 
    // If we did not found an empty slot, write next block, 
    // aka append to this file (directory :P) //
    // cofs_get_real_block() will allocate a new block //
    if (!(block_no = cofs_get_real_block(dir, num_blocks))) {
        printk("dir_link: INVALID BLOCK for name %s, num_blocks: %u!!!!!!!\n", name, num_blocks);
        return -1;
    }
    bh = sb_bread(dir->i_sb, block_no);
    cdir = (struct cofs_dirent *) bh->b_data;
    cdir->d_ino = ino;
    strncpy(cdir->d_name, name, COFS_FILE_MAX_LEN);
    mark_buffer_dirty(bh);
    brelse(bh); /// ?!
    
    // update the size of inode and flush it to disk //
    dir->i_size += COFS_BLOCK_SIZE;
    cofs_iput(dir);
    pr_debug("Added %s to inode %u. Increased the dir size to %llu\n", 
            name, ino, dir->i_size);

    return 0;
}

static int cofs_mknod(struct inode *dir, struct dentry *dentry, umode_t mode, dev_t dev)
{
    unsigned int m = mode & S_IFMT;
    struct inode *inode;

    // TODO - check if exists //
    inode = cofs_inode_alloc(dir->i_sb, m);
    inode->i_mode = mode;
    set_nlink(inode, 1);
    d_add(dentry, inode); // do we need this?
    if (m & S_IFDIR) {
        cofs_dir_link(inode, inode->i_ino, ".");    // add an entry to itself
        cofs_dir_link(inode, dir->i_ino, "..");     // add an entry to it's parent
    }
    cofs_dir_link(dir, inode->i_ino, dentry->d_name.name); // self link to parent

    pr_debug("cofs: mknod %s, mode: %d\n", dentry->d_name.name, mode & S_IFMT);
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
    .create         = cofs_create,
    .lookup         = cofs_lookup,
    //.link           = simple_link,
    //.unlink         = simple_unlink,
    //.symlink        = cofs_symlink,
    .mkdir          = cofs_mkdir,
    //.rmdir          = simple_rmdir,
    .mknod          = cofs_mknod,
    //.rename         = simple_rename,
};

struct file_operations cofs_dir_operations = {
    .llseek     = generic_file_llseek,
    //.read       = generic_read_dir,
    .iterate    = cofs_readdir,
    .fsync		= generic_file_fsync
};

