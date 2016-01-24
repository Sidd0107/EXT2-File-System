//Directories won't span multiple blocks nor have empty space btwn them

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
    //Checks that valid no of arguments are provided
    if(argc != 4) {
        fprintf(stderr, "Usage: ext2_cp <image file name> <file path on local disk> <file path on ext2 image>\n");
        exit(1);
    }
    // Gets details from command args
	char *dest_filename = extract_filename(argv[3]);
	char *dest_directory = strrchr(argv[3], '/');
	if (!dest_directory){
	  fprintf(stderr, "destination directory does not exist");
        return ENOENT;
	}
    //Open's file to be copied
	int fd = open(argv[1], O_RDWR);
    disk = mmap(NULL, 128 * BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(disk == MAP_FAILED) {
		perror("mmap");
		exit(1);
    }
    //Gets super block and group descriptor
	struct ext2_super_block *sb = (struct ext2_super_block *)(disk + BLOCK_SIZE);
	struct ext2_group_desc *gd = (struct ext2_group_desc *)  (disk + 2048 + ((sb -> s_block_group_nr) *  sizeof(struct ext2_group_desc)));
	//Gets file info
	struct stat fileinfo;
	if (stat(argv[2], &fileinfo) == -1){
		free(dest_filename);
		perror("stat for input file failed");
		exit(1);
	}
	
	dest_directory[(dest_directory == argv[3])? 1: 0] = '\0';
	//Checks if destination exists
	if(file_exists(argv[3], get_block(disk, gd -> bg_inode_table, BLOCK_SIZE), dest_filename)){
		free(dest_filename);
		fprintf(stderr, "cp: file exists");
		return -1;
	}
    
	//should be safe 
	unsigned int file_size = fileinfo.st_size;
	//equivalent to ceiling of file_size / 1024
	int num_blocks_required = (file_size > 0) ? ((file_size-1) >> 10) + 1 + ((file_size > BLOCK_SIZE * 12)? 1:0)  : 0;
	//CHECK FOR INODE TO RESERVE
	int free_inode;
	if((free_inode = find_first_free_bit(get_block(disk, gd -> bg_inode_bitmap, BLOCK_SIZE), 0, 128)) == -1){
		free(dest_filename);
		fprintf(stderr, "cp: operation failed: No free inodes");
		exit(1);
	}
	//CHECK FOR BLOCKS TO RESERVE
	if(gd -> bg_free_inodes_count == 0){
		free(dest_filename);
		fprintf(stderr, "Insufficient free inodes for file.");
		exit(1);
	}
    //Loops through iblocks of directory to make sure there is enough space
	int next_free = 0, free_block_counter = 0, num_free_blocks = 0, i;
	unsigned int inodes_to_reserve[num_blocks_required];
	while(num_free_blocks < num_blocks_required){
		if((next_free = find_first_free_bit(get_block(disk, gd -> bg_block_bitmap, BLOCK_SIZE),
												free_block_counter, 128)) == -1){
			free(dest_filename);
			fprintf(stderr, "Insufficient free space for file.");
			exit(1);
		}
		inodes_to_reserve[num_free_blocks++] = next_free;
		free_block_counter = next_free + 1;
	}
	if(num_free_blocks != num_blocks_required){
		free(dest_filename);
		fprintf(stderr, "Insufficient free space for file.");
		exit(1);
	}
	
	unsigned int filename_length = strlen(dest_filename);
	unsigned int dir_entry_size = (8 + filename_length) + (4 - (8 + filename_length) % 4) % 4;

	
	//CHECK FOR DIRECTORY TO RESERVE
	struct ext2_dir_entry_2  * new_file = malloc(dir_entry_size);
	if(!new_file){
		perror("malloc");
		exit(1);
	}
	new_file -> inode = free_inode + 1;
	new_file -> name_len = strlen(dest_filename);
	new_file -> rec_len = dir_entry_size;
	new_file -> file_type = 1;
	memcpy(new_file -> name, dest_filename, strlen(dest_filename));
	int new_capacity = gd -> bg_free_blocks_count - num_blocks_required;

    
	struct basic_fileinfo destination = find_file(argv[3], get_block(disk, gd -> bg_inode_table, BLOCK_SIZE));
    //Checks to see if destination is suitable
    //(directory, correct inode num(not 2 and >11))
	if(destination.inode < 11 && destination.inode != 2){
		free(dest_filename);
		fprintf(stderr, "Invalid destination");
		free(new_file);
		exit(1);
	}
	if(destination.type != 'd'){
		free(dest_filename);
		fprintf(stderr, "cp: destination is not a directory");
		free(new_file);
		exit(1);
	}
    // Reserves spot for and adds this entry into the directory
    // for the new file(error if not enough space)
	if ((new_capacity = reserve_directory_entry(get_block(disk, gd -> bg_inode_table, BLOCK_SIZE), destination.inode, new_capacity, new_file, get_block(disk, gd -> bg_block_bitmap, BLOCK_SIZE))) == -1){
		free(dest_filename);
		fprintf(stderr, "cp: Not enough capacity for file");
		exit(-1);
	}
	
	free(new_file);
	//reduces free inode count as new file takes up a new inode
	gd -> bg_free_inodes_count --;
	//Sets inode details for new file.
	set_inode(get_inode(get_block(disk, gd -> bg_inode_table, BLOCK_SIZE), free_inode + 1),
	free_inode, file_size, inodes_to_reserve, 
	num_blocks_required, 
	get_block(disk, gd -> bg_block_bitmap, BLOCK_SIZE),
	get_block(disk, gd -> bg_inode_bitmap, BLOCK_SIZE),
	EXT2_S_IFREG);
	gd -> bg_free_blocks_count = new_capacity;
    //Opens file for copying
	int copyfd = open(argv[2], O_RDONLY);
	if(copyfd == -1){
		perror("failed to open input file");
	}
    //Copies the file content into the new destination file
	int bytes_read = 0, current_bytes_read;
	for(i = 0; i < ((num_blocks_required <= 12)? num_blocks_required : num_blocks_required - 1); i++){
		char *current_block = get_block_from_inode(get_block(disk, gd -> bg_inode_table, BLOCK_SIZE), free_inode + 1, i);
		if ((current_bytes_read = read(copyfd, current_block, BLOCK_SIZE)) <= 0){
			perror("read");
			exit(1);
		}
		bytes_read += current_bytes_read;
	}
	printf("Success copying %s: copied %d bytes\n", dest_filename, bytes_read);
	close(copyfd);
	free(dest_filename);
	return 0;
}
