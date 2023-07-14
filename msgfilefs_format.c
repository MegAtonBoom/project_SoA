/*
 *	This file will write the following information onto the disk
 *	- BLOCK 0, superblock;
 *	- BLOCK 1, inode of the unique file (the inode for root is volatile);
 *	- BLOCK 2, ..., datablocks of the unique file 
 */

#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "msgfilefs.h"



int main(int argc, char *argv[])
{
	int fd, nbytes, i;
    uint64_t size;
	ssize_t ret;
	struct msgfs_sb_info sb;
	struct msgfs_inode root_inode;
	struct msgfs_inode file_inode;
	struct msgfs_dir_record  record;
    data_block *bd;



	if (argc != 2) {
		printf("The second parameter must be the image name");
		return -1;
	}

	fd = open(argv[1], O_RDWR);
	if (fd == -1) {
		perror("Error opening the device");
		return -1;
	}

    struct stat st;
	fstat(fd, &st);
	size = (st.st_size/DEFAULT_BLOCK_SIZE);

	//pack the superblock
	sb.version = 1;//file system version
	sb.magic = MAGIC;
    sb.block_size = DEFAULT_BLOCK_SIZE;
	sb.n_valid_blocks = 0;
	memset(sb.valid_blocks, 0, sizeof(sb.valid_blocks));
	ret = write(fd, (char *)&sb, sizeof(sb));

	if (ret != DEFAULT_BLOCK_SIZE) {
		printf("Bytes written [%d] are not equal to the default block size; sizeof = %ld\n", (int)ret, sizeof(sb));
		close(fd);
		return ret;
	}

	printf("Super block written succesfully\n");

	file_inode.inode_no = MSGFS_FILE_INODE_NUMBER;
	file_inode.file_size = size - 2;
	printf("File size is %ld\n",file_inode.file_size);
	fflush(stdout);
	ret = write(fd, (char *)&file_inode, sizeof(file_inode));

	if (ret != DEFAULT_BLOCK_SIZE) {
		printf("The file inode was not written properly.\n");
		close(fd);
		return -1;
	}
	bd = calloc(1, sizeof(data_block));
	printf("File inode written succesfully, size %ld.\n", sizeof(file_inode));
	
    INITIALIZE(bd)

	//for each datablock we write his offset as a metadata 
    for(i=2; i<size; i++){
        bd->bm.offset=i;
        ret = write(fd, (char *)bd, sizeof(data_block));
        if (ret != DEFAULT_BLOCK_SIZE) {
			printf("The file block numer %d was not written properly.\n", i);
			close(fd);
			
			return -1;
	    }   
    }
	
	free(bd);
    printf("File datablock has been written succesfully, each size: %ld.\n", sizeof(data_block));
	

    

	close(fd);

	return 0;
}
