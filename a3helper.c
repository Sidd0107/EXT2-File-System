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

extern unsigned char *disk;

/*
 * Used to parse a string representing a directory or regular file.
 * Forms a linked list structure where head = root, tail = file name.
 */
struct parsed_filename{
	char *file_name;
	struct parsed_filename *next;
};

/*
 * Return a pointer to a string equal to path + "/" + file_name.
 */
char *combine_path_and_file(char *path, char *file_name){
	int copy_location = 0;
	if(path[strlen(path) - 1] != '/'){
		copy_location = 1;
	}
	char *string = malloc((strlen(path) + strlen(file_name) + 1 + copy_location) * sizeof(char));
	if(!string){
		perror("malloc");
		exit(1);
	}
	memcpy(string, path, strlen(path));
	string[strlen(path)] = '/';
	memcpy(string + strlen(path) + copy_location, file_name, strlen(file_name));
	string[strlen(path) + strlen(file_name) + copy_location] = '\0';
	return string;
}

/*
 * Return non-zero value if (path/file_name) exists, 0 if it does not.
 * Need to pass in the inode base as well (pointer to inode 1(first)).
 */
int file_exists(char *path, char *inode_base, char *file_name){
	char *string = combine_path_and_file(path, file_name);
	struct basic_fileinfo fileinfo = find_file(string, inode_base);
	free(string);
	return fileinfo.inode;
}

// Parsing and searching functions

struct basic_fileinfo find_file(char *path, char *inode_base){
	struct basic_fileinfo file_info;
	file_info.type = '\0'; //default
	file_info.inode = 0;
	if(path[0] == '\0'){
		return file_info; //perhaps an error goes here?
	}
	else if (path[0] == '/' && path[1] == '\0'){
		file_info.type = 'd';
		file_info.inode = 2;
		return file_info;
	}
	struct parsed_filename *directory_linked_list = parse_directory(path);
	if(!directory_linked_list){
		return file_info;
	}
	return search_directory(inode_base, 2, directory_linked_list);
}

/*
 * Return a pointer to struct parsed_filename which contains the filename.
 * Path must begin with / and not be otherwise blank (i.e. path != '/').
 * If it does not meet this condition, NULL is returned.
 */
struct parsed_filename *parse_directory(char *path){
	if (path[0] != '/' || path[1] == '\0'){
		return NULL;
	}
	
	int i = 1;
	while (path[i] != '\0' && path[i] != '/'){
		i++;
	}
	
	struct parsed_filename *current_directory_filename = malloc(sizeof(struct parsed_filename));
	
	if(!current_directory_filename){
		perror("malloc for current directory filename");
   	}
	
	current_directory_filename -> file_name = malloc((i)*sizeof(char));
	if(!current_directory_filename -> file_name){
		perror("malloc for string in current directory struct");
		exit(1);
	}
	
	strncpy(current_directory_filename -> file_name, path + 1, i-1);
	current_directory_filename -> file_name[i-1] = '\0';
	
	current_directory_filename -> next = parse_directory(path + i);
	
	return current_directory_filename;
}

/*
 * Filename is actually null terminated. 
 * (0, '\0') will be returned on file not found. Valid as block 0 contains FS info and thus will not be used for any useful data.
 */
struct basic_fileinfo search_directory(char *inode_base, unsigned int inode_num, struct parsed_filename *filename){
	struct ext2_inode *current_inode = get_inode(inode_base, inode_num);
	unsigned int block_num = current_inode->i_block[0];
	
	unsigned int cur_index_serviced = 0, cur_inode_index = 0;
	char *current_block = get_block(disk, block_num, 1024);
	struct ext2_dir_entry_2 *current = (struct ext2_dir_entry_2 *) current_block;

	while(cur_index_serviced < current_inode -> i_size){
		current = (struct ext2_dir_entry_2 *) (current_block + (cur_index_serviced % 1024));
		if (strlen(filename -> file_name) == current -> name_len && !strncmp(filename -> file_name, current -> name, strlen(filename -> file_name))){
			if (!filename -> next){
			struct basic_fileinfo current_file;
				current_file.inode = current -> inode;
				current_file.type = type_to_char(current -> file_type);
				free(filename -> file_name);
				free(filename);
				return current_file;
			}
			else{
				struct parsed_filename *next = filename -> next;
				free(filename -> file_name);
				free(filename);
				return search_directory(inode_base, current -> inode, next);
			}
		}
		cur_index_serviced += current -> rec_len;
		if (cur_index_serviced < current_inode->i_size && cur_index_serviced % 1024 == 0){
			cur_inode_index++;
			current_block = get_block_from_inode(inode_base, inode_num, cur_inode_index);
		}
	}
	struct parsed_filename *current_filename = filename -> next, *prev = filename;
	free(prev -> file_name);
	free(prev);
	while(current_filename){
		prev = current_filename;
		current_filename = current_filename -> next;
		free(prev -> file_name);
		free(prev);
	}
	struct basic_fileinfo null_file;
	null_file.inode = 0;
	null_file.type = '\0';
	return null_file;
}


////////////////////////////



/*
 * type: The type value of a directory entry (0 to 7 inclusive)
 * Return a char representing the type of directory entry.
 */
char type_to_char(unsigned char type){
	switch(type){
		case 0:
			return '?'; //unknown type
		case 1:
			return 'f'; //regular file
		case 2:
			return 'd'; //directory
		case 3:
			return 'C'; //
		case 4:
			return 'B'; //
		case 5:
			return 'F'; //FIFO
		case 6:
			return 'S'; //
		case 7:
			return 'l'; //soft link
	}
	return '?';
}

// FS related functions; block and inode functions.


/*
 * Get the block with block number block_num.
 */
char *get_block(void *disk, unsigned int block_num, size_t block_size){
	return disk + block_size * block_num;
}

/*
 * inode_base: a pointer to inode number 1 ([0] in c indexing)
 * inode_number: the inode number (ext2 indexing)
 * Gets the inode with inode number inode_number.
 */
struct ext2_inode *get_inode(void *inode_base, unsigned int inode_number){
	return inode_base + (inode_number - 1) * sizeof(struct ext2_inode);
}


/**
 * Return the (inode_index)th block in inode_num (standard C indices).
 */
char *get_block_from_inode(char *inode_base, unsigned int inode_num, unsigned int inode_index){
	struct ext2_inode *current_inode;
	current_inode = get_inode(inode_base, inode_num);
	if (inode_index < 12){
		return get_block(disk, current_inode -> i_block[inode_index], 1024);
	}
	//assuming up to single indirect pointers!
	else{
		unsigned int *indirect_pointer_block = (unsigned int*) get_block(disk, current_inode -> i_block[12], 1024);
		return get_block(disk, indirect_pointer_block[inode_index - 12], 1024);
	}
}


int add_block_in_inode(struct ext2_inode *current_inode, char *block_bitmap, unsigned int inode_num){
	unsigned int new_inode_block_index = (current_inode -> i_size > 0)? ((((current_inode -> i_size) - 1) >> 10) + 1) : 0;
	if (new_inode_block_index >= 1024+12) {
		return -1;
	}
	int new_block = find_last_free_bit(block_bitmap, 128);
	if(new_block == -1){
		return -1;
	}
	set_bit(block_bitmap, new_block);
	if (new_inode_block_index <= 12) {
		current_inode -> i_block[new_inode_block_index] = new_block + 1;
		set_bit(block_bitmap, new_block);
	}
	if(new_inode_block_index == 12) {
		new_block = find_first_free_bit(block_bitmap, 0, 128);
		if(new_block == -1){
			unset_bit(block_bitmap, current_inode -> i_block[12] - 1);
			return -1;
		}
	}
	if(new_inode_block_index >= 12) {
		unsigned int *indirect_pointer_block = (unsigned int*) get_block(disk, current_inode -> i_block[12], 1024);
		indirect_pointer_block[new_inode_block_index - 12] = new_block + 1;
		set_bit(block_bitmap, new_block);
	}
	current_inode -> i_size += 1024;
	current_inode -> i_blocks += 2;
	return new_block + 1;
}


void set_inode(struct ext2_inode *inode, unsigned int inode_num, unsigned int size, unsigned *blocks, int num_blocks, char *block_bitmap, char *inode_bitmap, unsigned int mode){
	set_bit(inode_bitmap, inode_num);
	unsigned int i;
	inode -> i_size = size;
	inode -> i_blocks = (inode -> i_size > 0)? 2*((inode -> i_size - 1)/ 1024) + 2: 0;
	if(inode -> i_blocks >= 25){
		inode -> i_blocks += 2;
	}
	inode -> i_mode = 0x1ff | mode;
	if(num_blocks > 12){
		inode -> i_block[12] = blocks[num_blocks - 1] + 1;
		set_bit(block_bitmap, blocks[num_blocks - 1]);
	}
	for(i = 0; i < ((num_blocks <= 12)? num_blocks : num_blocks - 1); i++){
		set_block_in_inode(inode, blocks[i] + 1, i, block_bitmap);
	}
	inode -> i_links_count = 1;
}

void set_block_in_inode(struct ext2_inode* inode, unsigned block_number, unsigned inode_index, char *block_bitmap){
	if(inode_index < 12){
		inode -> i_block[inode_index] = block_number;
	}
	else{
		unsigned int* indirect_pointer_block = (unsigned int *) get_block(disk, inode -> i_block[12], 1024);
		indirect_pointer_block [inode_index - 12] = block_number;
	}
	set_bit(block_bitmap, block_number - 1);
}


/*
 * Get the bit'th bit in bitmap.
 */
char get_bit(char *bitmap, int bit){
	return (((bitmap[bit/8]) >> (bit%8)) & 1);
}

/*
 * Find the first unset bit in bitmap starting at startbit. -1 if none exist (all bits are 1).
 */
int find_first_free_bit(char *bitmap, int startbit, int bitmap_num_bits){
	int i;
	for (i = startbit; i < bitmap_num_bits; i++){
		if (!(*(bitmap+(i/8)) & (1<<(i%8)))){
			return i;
		}
	}
	return -1;
}

int find_last_free_bit(char *bitmap, int num_bits){
	int i;
	for(i = num_bits; i >= 0; i--){
		if (!(*(bitmap+(i/8)) & (1<<(i%8)))){
			return i;
		}
	}
	return -1;
}

/*
 * Set bit number bit in bitmap (to 1).
 */
void set_bit(char *bitmap, int bit){
	bitmap[bit/8] = bitmap[bit/8] | (1<<(bit%8));
}

/*
 * Unset bit number bit in bitmap (to 0).
 */
void unset_bit(char *bitmap, int bit){
	bitmap[bit/8] = bitmap[bit/8] & (~(1<<(bit%8)));
}

//reserve helpers

int reserve_directory_entry(char *inode_base, unsigned int inode_num, int num_free_blocks, struct ext2_dir_entry_2 *new_file, char *block_bitmap){
	struct ext2_inode *current_inode = get_inode(inode_base, inode_num);
	unsigned int block_num = current_inode -> i_block[0];	
	unsigned int cur_index_serviced = 0, cur_inode_index = 0;
	if(current_inode -> i_size > 0){
		char *current_block = get_block(disk, block_num, 1024);	
		struct ext2_dir_entry_2 *current = (struct ext2_dir_entry_2 *) current_block;
		while(cur_index_serviced < current_inode -> i_size){
			current = (struct ext2_dir_entry_2 *) (current_block + (cur_index_serviced % 1024));
			//According to ext2 spec directory entries must be aligned by 4's.
			int leftover_size = current -> rec_len - (((current -> name_len + 8) + (4 - (current -> name_len + 8) % 4) % 4));
			if(leftover_size >= new_file -> rec_len){
				current -> rec_len -= leftover_size;
				memcpy(((char *)current + (current -> rec_len)), new_file, new_file -> rec_len);
				current = (struct ext2_dir_entry_2 *) (((void *)current) + current -> rec_len);
				current -> rec_len = leftover_size;
				return num_free_blocks;
			}
			cur_index_serviced += current -> rec_len;
			if (cur_index_serviced < current_inode->i_size && cur_index_serviced % 1024 == 0){
				cur_inode_index++;
				current_block = get_block_from_inode(inode_base, inode_num, cur_inode_index);
			}
		}
	}
	if(num_free_blocks > 0){
		int new_block = add_block_in_inode(current_inode, block_bitmap, cur_inode_index);
		if(new_block <= 0){
			return -1;
		}	
		char *new_dir_entry_ptr = get_block(disk, new_block, 1024);
		memcpy(new_dir_entry_ptr, new_file, new_file -> rec_len);
		((struct ext2_dir_entry_2 *)new_dir_entry_ptr) -> rec_len = 1024;
		return num_free_blocks - 1;
	}
	return -1;
}

/*
 * If filename is not null terminated, do not use this function.
 */
char *extract_filename(char *filename){
	int i = 0, last_separator = 0;
	while(filename[i] != '\0'){
		if(filename[i] == '/'){
			last_separator = i + 1;
		}
		i++;
	}
	char *file_name = malloc(strlen(filename + last_separator) + 1);
	if(!file_name){
		perror("malloc");
		exit(1);
	}
	memcpy(file_name, filename + last_separator, strlen(filename + last_separator) + 1);
	return file_name;
}

void print_directory_contents(char *inode_base, unsigned int inode_num){
	int i;
	struct ext2_inode *current_inode = get_inode(inode_base, inode_num);
	unsigned int block_num = current_inode->i_block[0];
	unsigned int cur_index_serviced = 0, cur_inode_index = 0;
	char *current_block = get_block(disk, block_num, 1024);
	struct ext2_dir_entry_2 *current = (struct ext2_dir_entry_2 *)current_block;

	while(cur_index_serviced < current_inode -> i_size){
		current = (struct ext2_dir_entry_2 *) (current_block + (cur_index_serviced % 1024));
		for(i = 0; i < current -> name_len; i++){
			printf("%c", current -> name[i]);
		}
		printf("\n");
		cur_index_serviced += current -> rec_len;
		if (cur_index_serviced < current_inode->i_size && cur_index_serviced % 1024 == 0){
			cur_inode_index++;
			current_block = get_block_from_inode(inode_base, inode_num, cur_inode_index);
		}
	}
}
/*
 * Unset's all used bit's in an inode prior to deleting.
 */


void free_inode(struct ext2_inode *current_inode, void *disk, char *blockbitmap){
    unsigned int i_size = current_inode->i_size;
    unsigned int blocks = (i_size > 0)? ((i_size-1) / 1024) + 1: 0;
    int block = 0;
    for(block=0; block<blocks; block++){
        //free's indirect blocks
        if(block>=12){
            unsigned int* indirect_pointer_block = (unsigned int *) get_block(disk, current_inode->i_block[12], 1024);
            //indirect_pointer_block [inode_index - 12] = block_number;
            
             unset_bit(blockbitmap, indirect_pointer_block[block - 12] - 1);
             //free(indirect_pointer_block[block - 12]);
            
        }else{
            //free's direct blocks
            unset_bit(blockbitmap, current_inode->i_block[block] - 1);
            //free(current_inode->i_block[block-1]);
        }
        
    }
	if(blocks > 12){
		unset_bit(blockbitmap, current_inode->i_block[12] - 1);
    }
}




