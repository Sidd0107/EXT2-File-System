#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include "ext2.h"
#include "a3helper.h"

unsigned char *disk;

void print_directory_contents(char *inode_base, unsigned int inode_num);

int main(int argc, char **argv) {
    //Checks correct number number of arguments
    if(argc != 3) {
        fprintf(stderr, "Usage: ext2_ls <image file name> <path>\n");
        exit(1);
    }
    int fd = open(argv[1], O_RDWR);
	
	char *path = argv[2];

    disk = mmap(NULL, 128 * BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    //Checks to make sure mmap didnt fail.
    if(disk == MAP_FAILED) {
	perror("mmap");
	exit(1);
    }
	//Gets superblock and group descriptor(In this assignment there is only 1 group descr.)
    struct ext2_super_block *sb = (struct ext2_super_block *)(disk + BLOCK_SIZE);
	struct ext2_group_desc *gd = (struct ext2_group_desc *)  (disk + 2*BLOCK_SIZE + ((sb -> s_block_group_nr) *  sizeof(struct ext2_group_desc)));
	//Gets details about the file in path.
	struct basic_fileinfo fileinfo = find_file(path, get_block(disk, gd -> bg_inode_table, BLOCK_SIZE));
	//Makes sure file type is directory and calls helper to print contents of that directory.
	if(fileinfo.type == 'd'){
		print_directory_contents(get_block(disk, gd -> bg_inode_table, BLOCK_SIZE), fileinfo.inode);
	}//If type is file handle appropriately to make sure it prints error or the file name
	else if(fileinfo.type == 'f'){
		if(path[strlen(path)-1] != '/'){
			printf("%s\n", path);
		}
		else{
			printf("ls: cannot access %s: Not a directory\n", path);
		}
	}
	else{
		printf("ls: cannot access %s: No such file or directory\n", path);
	}
	return 0;
}