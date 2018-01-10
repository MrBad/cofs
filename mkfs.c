/**
 * Creates the file system
 * Inspired from Unix V6 and xv6 reimplementation
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <libgen.h>
#include <linux/fs.h>

#include "cofs_common.h"

/* right now I do not read the partition table
 * we will hardcode this sector as the start of COFS 
 * partition.
 * #define PARTITION_OFFSET 204801
 */
#define PARTITION_OFFSET 0

int fd;
struct cofs_superblock sb;
uint32_t free_block = 0;

void write_block(uint32_t block, void *buf)
{
    block += PARTITION_OFFSET;
    if (lseek(fd, block * COFS_BLOCK_SIZE, 0) < 0) {
        perror("lseek");
        exit(1);
    }
    if (write(fd, buf, COFS_BLOCK_SIZE) != COFS_BLOCK_SIZE) {
        perror("write");
        exit(1);
    }
}

void read_block(uint32_t block, void *buf)
{
    block += PARTITION_OFFSET;
	if (lseek(fd, block * COFS_BLOCK_SIZE, 0) < 0) {
		perror("lseek");
		exit(1);
	}
	if (read(fd, buf, COFS_BLOCK_SIZE) != COFS_BLOCK_SIZE) {
		perror("read");
		exit(1);
	}
}

void write_inode(uint32_t inum, cofs_inode_t *dino)
{
	uint8_t buf[COFS_BLOCK_SIZE];
	uint32_t block = INO_BLOCK(inum, sb); // block which contains this inum
	read_block(block, buf);
	cofs_inode_t *inode = ((cofs_inode_t*)buf) + (inum % NUM_INOPB);
	*inode = *dino; //
	write_block(block, buf);
}

void read_inode(unsigned int inum, cofs_inode_t *dino)
{
	char buf[COFS_BLOCK_SIZE];
	uint32_t block = INO_BLOCK(inum, sb);
	read_block(block, buf);
	cofs_inode_t *inode = ((cofs_inode_t *)buf) + (inum % NUM_INOPB);
	*dino = *inode;
}

uint32_t inode_alloc(uint16_t type)
{
    static uint32_t free_inode = 1;
	uint32_t inum = free_inode++;
	struct cofs_inode dino;
	memset(&dino, 0, sizeof(dino));
    dino.type = type;
	dino.num_links = 1;
	dino.size = 0;
	write_inode(inum, &dino);

	return inum;
}

// mark bitmap as used up to block //
void block_alloc(uint32_t used)
{
	char buf[COFS_BLOCK_SIZE];
	uint32_t i;
	uint32_t bitmap_block;
	for (bitmap_block = 0; bitmap_block < (used / BITS_PER_BLOCK); bitmap_block++) {
	    printf("Writing bitmap block %u\n", bitmap_block);
	    memset(&buf, 0xFF, sizeof(buf));
	    write_block(sb.bitmap_start + bitmap_block, buf);
	}
	used = used % BITS_PER_BLOCK;
	memset(&buf, 0, COFS_BLOCK_SIZE);
	for(i = 0; i < used; i++) {
		buf[i / 8] = buf[i / 8] | (0x1 << ( i % 8));
	}
	write_block(sb.bitmap_start + bitmap_block, buf);
}

#define min(a, b) ((a) < (b) ? (a) : (b))

// appends from ptr, size bytes int inode number inum
void inode_append(uint32_t inum, void *ptr, uint32_t size)
{
	char *p = (char *) ptr;
	char buf[COFS_BLOCK_SIZE];
	struct cofs_inode dino;
	uint32_t sind_buf[COFS_BLOCK_SIZE/sizeof(uint32_t)]; // single indirect buffer
    uint32_t dind_buf[COFS_BLOCK_SIZE/sizeof(uint32_t)]; // double indirect buffer
	uint32_t offset, file_block_number, block_no, n;

	read_inode(inum, &dino);
	offset = dino.size;
	while(size > 0) {
		file_block_number = offset / COFS_BLOCK_SIZE;
		if(file_block_number > MAX_FILE_SIZE) {
			printf("File too large > %lu blocks\n", MAX_FILE_SIZE);
			exit(1);
		}
		// direct //
		if (file_block_number < NUM_DIRECT) {
			if(dino.addrs[file_block_number] == 0) {
				dino.addrs[file_block_number] = free_block++;
			}
			block_no = dino.addrs[file_block_number];
		}
		// single indirect //
		else if(file_block_number < NUM_DIRECT + NUM_SIND){
			if(dino.addrs[SIND_IDX] == 0) { // alloc a block for single indirect
				dino.addrs[SIND_IDX] = free_block++;
			}
			read_block(dino.addrs[SIND_IDX], sind_buf);
			if(sind_buf[file_block_number - NUM_DIRECT] == 0) {
				sind_buf[file_block_number - NUM_DIRECT] = free_block++;
				write_block(dino.addrs[SIND_IDX], sind_buf);
			}
			block_no = sind_buf[file_block_number - NUM_DIRECT];
		}
		// double indirect //
		else {
			if(dino.addrs[DIND_IDX] == 0) { // alloc a block for double indirect
				dino.addrs[DIND_IDX] = free_block++;
			}
			uint32_t rel_b = file_block_number - NUM_DIRECT - NUM_SIND;
			uint32_t midx = rel_b / NUM_SIND;
			uint32_t sidx = rel_b % NUM_SIND;
			read_block(dino.addrs[DIND_IDX], sind_buf);
			if(sind_buf[midx] == 0) { // alloc 1'st level block
				sind_buf[midx] = free_block++;
				write_block(dino.addrs[DIND_IDX], sind_buf);
			}
			read_block(sind_buf[midx], dind_buf);
			if(dind_buf[sidx] == 0) { // alloc 2'nd level block
				dind_buf[sidx] = free_block++;
				write_block(sind_buf[midx], dind_buf);
			}
			block_no = dind_buf[sidx];
		}
		n = min(size, (file_block_number + 1) * COFS_BLOCK_SIZE - offset);
		read_block(block_no, buf);
		memcpy(buf + offset - (file_block_number * COFS_BLOCK_SIZE), p, n);
		write_block(block_no, buf);
		size -= n;
		offset += n;
		p += n;
	}
	// printf(">>>%d\n", offset);
	dino.size = offset;
	write_inode(inum, &dino);
}

int main(int argc, char *argv[])
{
	if(argc < 2) {
		printf("Usage:\n %s <image> <files..>\n\n"
		        "Options:\n"
		        " image - image to format (file or device)\n"
		        " files - optional space separated list of files to be copied to partition\n",
		            argv[0]);

		return 1;
	}

	printf("Max supported file size: %lu bytes\n", MAX_FILE_SIZE * COFS_BLOCK_SIZE);
	struct stat st;
	uint32_t cofs_size,		// total fs size in blocks
	         bitmap_size,	// free bitmap size in blocks
	         inodes_size,	// size of inodes in blocks
	         num_inodes,
	         num_meta_blocks,
	         num_data_blocks;

	uint32_t i;
    char buf[COFS_BLOCK_SIZE];
	struct cofs_inode dino;
	struct cofs_dirent dir;

	if (sizeof(int) != 4) {
		printf("Sizeof int should be 4, got %lu\n", sizeof(int));
		return 1;
	}
	if (COFS_BLOCK_SIZE % sizeof(cofs_inode_t) != 0) {
		printf("Block size is not multiple of inode size\n");
		return 1;
	}
	if (COFS_BLOCK_SIZE % sizeof(sizeof(struct cofs_dirent)) != 0) {
		printf("Block size is not multiple of dirent size\n");
		return 1;
	}
	if ((fd = open(argv[1], O_RDWR, 0666)) < 0) {
		perror("open");
		return 1;
	}
	if (fstat(fd, &st) < 0) {
		printf("Cannot stat %s\n", argv[1]);
		close(fd);
		return 1;
	}
    if (S_ISREG(st.st_mode)) {
    	cofs_size = st.st_size / COFS_BLOCK_SIZE;
    } else if (S_ISBLK(st.st_mode)) {
        uint64_t size;
        if (ioctl(fd, BLKGETSIZE64,&size) == -1) {
            perror("size");
            close(fd);
            return 1;
        }
        cofs_size = size / COFS_BLOCK_SIZE;
    }
	cofs_size -= PARTITION_OFFSET;
	// assuming one file has ~4096 bytes, 1 inode per file //
	num_inodes = cofs_size * COFS_BLOCK_SIZE / 4096; 
	bitmap_size = 1 + cofs_size / BITS_PER_BLOCK;
	inodes_size = 1 + num_inodes / NUM_INOPB;

	// 1'st block unused, 2'nd block superblock //
	num_meta_blocks = 2 + inodes_size + bitmap_size;
	num_data_blocks = cofs_size - num_meta_blocks;

	sb.magic = COFS_MAGIC;
	sb.size = cofs_size;
	sb.num_blocks = num_data_blocks;
	sb.num_inodes = num_inodes;
	sb.bitmap_start = 2;
	sb.inode_start = 2 + bitmap_size;
	sb.data_block = num_meta_blocks;
	free_block = num_meta_blocks;

	printf("Superblock:\n"
	        " Block size: %u\n"
	        " Size: %u blocks\n"
	        " Data blocks: %u blocks\n"
	        " Number of inodes: %u\n"
	        " Block bitmap starts at: %u block\n"
	        " Inode table starts at: %u block\n"
	        " Size of partition meta data: %u blocks\n"
	        " First data block: %u\n",
		COFS_BLOCK_SIZE, sb.size, sb.num_blocks, sb.num_inodes, 
		sb.bitmap_start, sb.inode_start, sb.data_block, num_meta_blocks);

	// check if we already have cofs fs //
	read_block(1, buf);
	if(((uint32_t*) buf) [0] == COFS_MAGIC) {
		printf("Already formated\n");
		// exit(0);
	}
	// bzero fs //
	memset(buf, 0, COFS_BLOCK_SIZE);
	for(i = 0; i < sb.size; i++) {
		write_block(i, buf);
	}
	// write superblock //
	memcpy(buf, (void *)&sb, sizeof(sb));
	write_block(1, buf);

	// root inode //
    uint32_t root_inode = inode_alloc(FS_DIRECTORY | 0755);
	if (root_inode != 1) {
		printf("Invalid root inode - expected 1\n");
		exit(1);
	}
	memset(&dir, 0, sizeof(dir));
	dir.d_ino = root_inode;
	strcpy(dir.d_name, ".");
	inode_append(root_inode, &dir, sizeof(dir));

	memset(&dir, 0, sizeof(dir));
	dir.d_ino = root_inode;
	strcpy(dir.d_name, "..");
	inode_append(root_inode, &dir, sizeof(dir));
	
	int file_fd, inode_num, num_bytes;
	for(i = 2; i < (unsigned int) argc; i++) {
		if((file_fd = open(argv[i], O_RDONLY)) < 0) {
			perror(argv[i]);
			exit(1);
		}
		inode_num = inode_alloc(FS_FILE | 0666);
		memset(&dir, 0, sizeof(dir));
		dir.d_ino = inode_num;
		strncpy(dir.d_name, basename(argv[i]), COFS_FILE_NAME_MAX_LEN);
		inode_append(root_inode, &dir, sizeof(dir));

		while ((num_bytes = read(file_fd, buf, sizeof(buf))) > 0) {
			inode_append(inode_num, buf, num_bytes);
		}
		close(file_fd);
	}

	read_inode(root_inode, &dino);

	uint32_t offset = dino.size;
	offset = ((offset / COFS_BLOCK_SIZE) + 1) * COFS_BLOCK_SIZE;
	dino.size = offset;
	write_inode(root_inode, &dino);
	block_alloc(free_block);

	printf("First free block is %d\n", free_block);
	close(fd);

	return 0;
}
