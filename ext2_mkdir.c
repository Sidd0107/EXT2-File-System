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
#include <sys/stat.h>
#include <errno.h>

unsigned char *disk;

int main(int argc, char **argv) {

    //Checks valid no of argument
    if(argc != 3) {
        fprintf(stderr, "Usage: ext2_mkdir <image file name> <file path on ext2 image>\n");
        exit(1);
    }
    //Gets arguments
    int fd = open(argv[1], O_RDWR);
    char *path = argv[2];
    char *dirName = extract_filename(argv[2]);
    
    //maps disk in memory and checks for error
    disk = mmap(NULL, 128 * BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(disk == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
    //gets superblock and groupd desc.
    struct ext2_super_block *sb = (struct ext2_super_block *)(disk + BLOCK_SIZE);
    struct ext2_group_desc *gd = (struct ext2_group_desc *)  (disk + 2*BLOCK_SIZE + ((sb -> s_block_group_nr) *  sizeof(struct ext2_group_desc)));
    // Checks if file already exists and returns if yes.
	char *directory = strrchr(argv[2], '/');
	directory[(argv[2] == directory)? 1:0] = '\0';
    if(file_exists(path, get_block(disk, gd -> bg_inode_table, BLOCK_SIZE), dirName)!=0){
        fprintf(stderr, "mkdir: file exists\n");
        return EEXIST;
    }
    //CHECK FOR INODE TO RESERVE
    int free_inode;
    if((free_inode = find_first_free_bit(get_block(disk, gd -> bg_inode_bitmap, BLOCK_SIZE), 0, 128)) == -1){
        fprintf(stderr, "mkdir: operation failed: No free inodes");
        exit(1);
    }
    unsigned int dirname_length = strlen(dirName);
    unsigned int dir_entry_size = (8 + dirname_length) + (4 - (8 + dirname_length) % 4) % 4;
    //Creates new directory entry
    struct ext2_dir_entry_2  * new_dir = malloc(dir_entry_size);
	if(!new_dir){
		perror("malloc");
		exit(1);
	}
    new_dir -> inode = free_inode + 1;
    new_dir -> name_len = strlen(dirName);
    new_dir -> rec_len = dir_entry_size;
    new_dir -> file_type = EXT2_FT_DIR;
    memcpy(new_dir -> name, dirName, strlen(dirName));
    int new_capacity = gd -> bg_free_blocks_count;
    
    //Getting info to check that destination is a directory and not reserved
    struct basic_fileinfo destination = find_file(argv[2], get_block(disk, gd -> bg_inode_table, BLOCK_SIZE));
    if(destination.inode < 11 && destination.inode != 2){
        fprintf(stderr, "Invalid destination");
        free(new_dir);
        return ENOENT;
    }//Checks if destination is a directory
    if(destination.type != 'd'){
        fprintf(stderr, "mkdir: destination is not a directory");
        free(new_dir);
        return ENOENT;
   }//Checks if directory has space for the new directory
	if(new_capacity < 1){
		fprintf(stderr, "mkdir: not enough capacity");
		return ENOSPC;
	}
    //Reserves location for new directory entry
    if ((new_capacity = reserve_directory_entry(get_block(disk, gd -> bg_inode_table, BLOCK_SIZE), destination.inode, new_capacity, new_dir, get_block(disk, gd -> bg_block_bitmap, BLOCK_SIZE))) == -1){
        fprintf(stderr, "mkdir: Not enough capacity for file");
        exit(-1);
    }
    //freeing memory and reducing the free inode count
    free(new_dir);
    gd -> bg_free_inodes_count --;
    set_inode(get_inode(get_block(disk, gd -> bg_inode_table, BLOCK_SIZE), free_inode + 1),
              free_inode, 0, NULL,
              0,
              get_block(disk, gd -> bg_block_bitmap, BLOCK_SIZE),
              get_block(disk, gd -> bg_inode_bitmap, BLOCK_SIZE),
		EXT2_S_IFDIR);
    gd -> bg_free_blocks_count = new_capacity;
    printf("Success: Directory entry created.");
    //Adds . directory to the new directory entry
	new_dir = malloc(12);
	if (!new_dir){
		perror("malloc");
		exit(1);
	}
	new_dir -> inode = free_inode + 1;
	new_dir -> name_len = 1; 
	new_dir -> rec_len = 12;
	new_dir -> file_type = EXT2_FT_DIR;
	new_dir -> name[0] = '.';

	struct ext2_inode *inode = get_inode(get_block(disk, gd -> bg_inode_table, BLOCK_SIZE), free_inode + 1);
	inode -> i_block[0] = 0;
    // Adds .. to the new directory entry
	new_capacity = reserve_directory_entry(get_block(disk, gd -> bg_inode_table, BLOCK_SIZE), free_inode + 1, new_capacity, new_dir, get_block(disk, gd -> bg_block_bitmap, BLOCK_SIZE));
	new_dir -> inode = destination.inode;
	new_dir -> name_len = 2;
	new_dir -> name[0] = '.';
	new_dir -> name[1] = '.';
	reserve_directory_entry(get_block(disk, gd -> bg_inode_table, BLOCK_SIZE), free_inode + 1, new_capacity, new_dir, get_block(disk, gd -> bg_block_bitmap, BLOCK_SIZE));
	gd -> bg_free_blocks_count = new_capacity;
	gd -> bg_used_dirs_count += 3;
	free(new_dir);
	free(dirName);
	return 0;
}