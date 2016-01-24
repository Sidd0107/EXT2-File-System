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
    //Makes sure correct number of arguments
    if(argc != 4) {
        fprintf(stderr, "Usage: ext2_cp <image file name> <file location> <link location>\n");
        exit(1);
    }
    //Extracts filename and checks if it is not too long
	char *linked_filename = extract_filename(argv[3]);
	if(strlen(linked_filename) > 256){
		fprintf(stderr, "filename too long!");
		free(linked_filename);
		exit(1);
	}
    //Gets path from arg
	char *file_path;
	if(argv[3][0] != '/'){
		fprintf(stderr, "Invalid link destination!");
		exit(1);
	}
	file_path = strrchr(argv[3], '/');
	file_path[(file_path == argv[3])? 1 : 0] = '\0';
	char *link_full_path = combine_path_and_file(argv[3], linked_filename);
	
	//Opens and maps diskimage to memory
    int fd = open(argv[1], O_RDWR);
    disk = mmap(NULL, 128 * BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(disk == MAP_FAILED) {
		perror("mmap");
		exit(1);
    }
    //Gets superblock and group descriptor
	struct ext2_super_block *sb = (struct ext2_super_block *)(disk + BLOCK_SIZE);
	struct ext2_group_desc *gd = (struct ext2_group_desc *)  (disk + 2*BLOCK_SIZE + ((sb -> s_block_group_nr) *  sizeof(struct ext2_group_desc)));
	
    //Makes sure the file exists
	if(file_exists(link_full_path, get_block(disk, gd -> bg_inode_table, BLOCK_SIZE), linked_filename)){
		fprintf(stderr, "ln: file at link location exists");
		return EEXIST;
	};
	
	struct basic_fileinfo file_to_link = find_file(argv[2], get_block(disk, gd -> bg_inode_table, BLOCK_SIZE));
	struct basic_fileinfo destination_dir = find_file(argv[3], get_block(disk, gd -> bg_inode_table, BLOCK_SIZE));
    //Checks validity of destination and file to be linked
	if(destination_dir.inode == 0){
		fprintf(stderr, "ln: destination directory does not exist");
		return ENOENT;
	}
	else if (destination_dir.type != 'd'){
		fprintf(stderr, "ln: destination is not a directory!");
		return EISDIR;
	}
	
	if(file_to_link.inode == 0){
		fprintf(stderr, "ln: file to link does not exist");
		return ENOENT;
	}
	else if (file_to_link.type != 'f'){
		fprintf(stderr, "ln: trying to link to non-regular file!");
		return EISDIR;
	}

	unsigned int filename_length = strlen(linked_filename);
	unsigned int dir_entry_size = (8 + filename_length) + (4 - ((8 + filename_length) % 4)) % 4;


	//CHECK FOR DIRECTORY TO RESERVE
    //Creates new dir entry element with specific details
	struct ext2_dir_entry_2  * new_file = malloc(dir_entry_size);
	if(!new_file){
		perror("malloc");
		exit(1);
	}
	new_file -> inode = file_to_link.inode;
	new_file -> name_len = strlen(linked_filename);
	new_file -> rec_len = dir_entry_size;
	new_file -> file_type = 1;
	memcpy(new_file -> name, linked_filename, strlen(linked_filename));
    
	// Gets space and adds directory entry to it
	if (reserve_directory_entry(get_block(disk, gd -> bg_inode_table, BLOCK_SIZE), destination_dir.inode, gd -> bg_free_blocks_count, new_file, get_block(disk, gd -> bg_block_bitmap, BLOCK_SIZE)) == -1){
		fprintf(stderr, "Failed to create directory: not enough space for directory entry");
	}
	free(new_file);
    // Gets inode of original file being linked and increments the link count
    // As a new hard link has been created.
    struct ext2_inode *inode = get_inode(get_block(disk, gd -> bg_inode_table, BLOCK_SIZE), file_to_link.inode);
    inode -> i_links_count++;
	printf("link to %s at %s successfully created! %s", argv[2], link_full_path, linked_filename);
	free(linked_filename);
	free(link_full_path);
	return 0;
}
