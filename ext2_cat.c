#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include "ext2.h"
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include "a3helper.h"

unsigned char *disk;


int main(int argc, char **argv){
	if(argc != 3){
		fprintf(stderr, "usage: printblock <image file> <path>");
	}
	int fd = open(argv[1], O_RDONLY);
	disk = mmap(NULL, 128*BLOCK_SIZE, PROT_READ, MAP_SHARED, fd, 0);
	if(disk == MAP_FAILED){
		perror("mmap");
		exit(1);
	}
	struct ext2_super_block *sb = (struct ext2_super_block *) (disk+BLOCK_SIZE);
	struct ext2_group_desc *gd = (struct ext2_group_desc *) (disk+2048);
	struct basic_fileinfo file_info = find_file(argv[2], get_block(disk, gd -> bg_inode_table, BLOCK_SIZE));
	if (file_info.inode < 11 && file_info.inode != 2){
		fprintf(stderr, "Invalid file! Does not exist.");
		return 1;
	}
	else if (file_info.type != 'f'){
		fprintf(stderr, "Not a file.");
		return 1;
	}
	int inode_num = file_info.inode;
	struct ext2_inode *current_inode = get_inode(get_block(disk, gd -> bg_inode_table, BLOCK_SIZE), inode_num);
	int i, j, num_bytes_to_read, num_blocks = (current_inode -> i_size > 0)? ((current_inode -> i_size - 1) >> 10) + 1 : 0;
	for (i=0;i < num_blocks;i++){
		num_bytes_to_read = ((i+1)*BLOCK_SIZE < current_inode -> i_size)? BLOCK_SIZE: current_inode -> i_size % BLOCK_SIZE;
		char *block_to_read = get_block_from_inode(get_block(disk, gd -> bg_inode_table, BLOCK_SIZE), inode_num, i);
		for(j=0; j<num_bytes_to_read;j++){
			printf("%c", block_to_read[j]);
		}
	}
	return 0;
}
