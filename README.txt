This directory is a Linux kernel module for cOSiris file system
(i call it cofs) so we can mount the cofs file system type directly into Linux.
The work is in progress...
You can run it from inside cOSiris/utils/cofs, by typing make run
This will compile (hopefully) the module cofs.ko, insert it into kernel, 
and try to mount cOSiris hdd.img (../../hdd.img) on /mnt. 
This hdd.img image is created in cOSiris directory by typing 'make diskimg'. 
It's more like mkfs.cofs, an utility that i will write,
but one at a time :D
to unmount and clean all, type 'make clean'.

// the cofs (disk) super block //
typedef struct cofs_superblock
{
    unsigned int magic;
    unsigned int size;              // total size of fs, in blocks
    unsigned int num_blocks;        // number of data blocks
    unsigned int num_inodes;        // number of inodes
    unsigned int bitmap_start;      // where free bitmap starts, in blocks
    unsigned int inode_start;       // where inodes starts, in blocks
                                    // TODO: add where data blocks starts
                                    // so we ensure we never allocate blocks
                                    // in bitmap or inoode zone
} cofs_superblock_t;


Let's say partitionSize is 4 * 1024 * 1024 = 4MB
And blocksize is 512
number of blocks = 8192
number of bytes in bitmap = 8196 / 8  = 1024 
number of blocks in bitmap = 1024 / 512 = 2

Some `partition` geometry:

offsetInBlocks      name                sizeInBlocks

0                   unused, boot        1
1                   sb - superblock     1
2                   sb.bitmap_start     partitionSizeInBlocks / (8 * BLOCKSIZE) + 1 reserved
2 + bitmapSize      sb.inode_start      (sb.num_inodes / NUM_INOPB) / BLOCKSIZE
sb free nodes



// inode structure //
typedef struct cofs_inode {
    unsigned short int type;            // type of inode - file, link, directory.
    unsigned short int major;         // for devices, major, minor
    unsigned short int minor;
    unsigned short int uid;           // user id
    unsigned short int gid;           // group id
    unsigned short int num_links;     // number of links to this inode, (aka ln
  -s or dir)
    unsigned int atime, mtime, ctime;   // infos about time, not really used right now
    unsigned int size;                  // file size, in bytes
      // Space for direct blocks to data, SIND_IDX, DIND_IDX
      // + 1 reserved for future triple indx
    unsigned int addrs[NUM_DIRECT + 3];
} cofs_inode_t;

The inode structure is a disk representation of an inode.
I usually call it dino in codes, to remind that is a disk representation of it, and not a Linux struct inode. Anyway, they both share the same inode id.

TODO - inode->type in cOSiris is ambiguous somehow, it stores the type of file, but  file permissions... Yeah :D
We need to add this, and maybe change this field to file->mode; 
Will simplify and clarify the sysfile.c, also which use it.
So, permissions are not so clear right now.
Maybe this linux module will help me to continue developing on it.

// inode - what's with indirections
An inode will store references to file data blocks more like in ext2;
First NUM_DIRECT (6 right now) COFS_BLOCK_SIZE blocks are stored directly in inode addrs[], for small files
Next NUM_SIND blocks are stored in an indirect table, which block number is located at addrs[SIND_IDX]
The rest of the file, until MAX_FILE_SIZE blocks are stored in double indirect table. 
With 512 bytes per COFS_BLOCK_SIZE, MAX_FILE_SIZE will be (NUM_DIRECT + NUM_SIND + NUM_DIND) * 512
6 + 128 + 128*128 * 512 / 1024 ~ 8MB, enough for now :)


