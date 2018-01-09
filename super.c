#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/statfs.h>
#include <linux/buffer_head.h>
#include <linux/slab.h>

#include "cofs_common.h"
#include "inode.h"

cofs_superblock_t *cofs_super_block_read(struct super_block *sb)
{
    struct buffer_head *bh;

    cofs_superblock_t *cofs_sb = kzalloc(sizeof(cofs_superblock_t), GFP_NOFS);

    if(!cofs_sb) {
        pr_err("cofs: cannot allocate super block\n");
        return NULL;
    }

    bh = sb_bread(sb, 1);
    if (!bh) {
        pr_err("cofs: cannot read block 1\n");
        kfree(cofs_sb);
        return NULL;
    }
    pr_debug("buffer_head size: %lu, sb size: %lu\n", bh->b_size, sb->s_blocksize);
    // fill data //
    // how about disk to cpu and cpu 2 disk conversion little/big endian...
    memcpy(cofs_sb, bh->b_data, sizeof(cofs_superblock_t));
    brelse(bh);
    
    pr_debug("Magic is: %X\n", cofs_sb->magic);
    pr_debug("Size in blocks: %d\n", cofs_sb->size);
    pr_debug("Number of data blocks: %d\n", cofs_sb->num_blocks);
    pr_debug("Number of inodes: %d\n", cofs_sb->num_inodes);
    pr_debug("Bitmap starts at: %d\n", cofs_sb->bitmap_start);
    pr_debug("Innode starts at: %d\n", cofs_sb->inode_start);

    if (cofs_sb->magic != COFS_MAGIC) {
        pr_err("cofs: invalid filesystem, wrong magic number %X\n", cofs_sb->magic);
        kfree(cofs_sb);
        return NULL;
    }

    return cofs_sb;
}

static void cofs_put_super(struct super_block *sb) {
    pr_debug("cofs: put super\n");
    kfree(sb->s_fs_info);
}

int cofs_statfs(struct dentry *dentry, struct kstatfs *statfs)
{
    statfs->f_type = COFS_MAGIC;
    statfs->f_bsize = COFS_BLOCK_SIZE;
    statfs->f_bfree = 123;
    statfs->f_namelen = 28;
    return 0;
}

struct super_operations cofs_super_ops = {
    .evict_inode    = cofs_inode_evict,
    .statfs         = cofs_statfs, 
    .put_super      = cofs_put_super,
};

static int cofs_fill_sb(struct super_block *sb, void *data, int silent)
{

	cofs_superblock_t *cofs_sb;
	struct inode *root;
    // Make sure a block is a set of COFS_BLOCK_SIZE //
	if (sb_set_blocksize(sb, COFS_BLOCK_SIZE) == 0) {
		pr_err("cofs: cannot set device's blocksize to %d\n", COFS_BLOCK_SIZE);
		return -EINVAL;
	}
    
    cofs_sb = cofs_super_block_read(sb);

    pr_debug("cofs: filling super_block\n");
	if (!cofs_sb)
		return -EINVAL;

	sb->s_magic = cofs_sb->magic;
	sb->s_fs_info = cofs_sb;
	sb->s_op = &cofs_super_ops;
	sb->s_maxbytes = MAX_FILE_SIZE;
    
	root = cofs_iget(sb, 1);
	pr_debug("root has %u i_nlink\n", root->i_nlink);
	if (IS_ERR(root))
	    return PTR_ERR(root);
	sb->s_root = d_make_root(root);
	if (!sb->s_root) {
	    iput(root); // ?!?!?
	    kfree(cofs_sb);
		pr_err("cofs cannot create root\n");
		return -ENOMEM;
	}
	
	return 0;
}

static struct dentry * cofs_mount(
        struct file_system_type *type, 
        int flags, 
        const char *dev, 
        void *data) 
{
    struct dentry *entry = mount_bdev(type, flags, dev, data, cofs_fill_sb);

    pr_debug("request to mount: %s, dev: %s\n", type->name, dev);

    if (IS_ERR(entry))
        pr_err("cofs mounting failed\n");
    else
        pr_debug("cofs mounted\n");

    return entry;

}

static struct file_system_type cofs_type = {
    .owner = THIS_MODULE,
    .name = "cofs",
    .mount = cofs_mount,
    .kill_sb = kill_block_super,
    .fs_flags = FS_REQUIRES_DEV
};

static int __init cofs_init(void)
{
    pr_debug("cofs: init\n");
    return register_filesystem(&cofs_type);
}

static void __exit cofs_exit(void) 
{
    if (unregister_filesystem(&cofs_type) != 0) {
        pr_err("cofs: cannot unregister_filesystem\n");
    }
    pr_debug("cofs: unloaded\n");
}

module_init(cofs_init);
module_exit(cofs_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MrBadNewS");

