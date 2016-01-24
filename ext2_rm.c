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
#include <libgen.h>
#include <errno.h>

unsigned char *disk;

int main(int argc, char **argv) {
    //Checks valid no of argument
    if(argc != 3) {
        fprintf(stderr, "Usage: ext2_rm <image file name> <file path on ext2 image>\n");
        exit(1);
    }
    int fd = open(argv[1], O_RDWR);
    char *path = argv[2];
    //maps disk in memory
    disk = mmap(NULL, 128 * BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(disk == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
    struct ext2_super_block *sb = (struct ext2_super_block *)(disk + BLOCK_SIZE);
    struct ext2_group_desc *gd = (struct ext2_group_desc *)  (disk + 2*BLOCK_SIZE + ((sb -> s_block_group_nr) *  sizeof(struct ext2_group_desc)));
    
        
    char *dirc, *basec, *dirName, *pth;
    dirc = strdup(path);
    basec = strdup(path);
    pth = dirname(dirc);
    dirName = basename(basec);

    
    // Checks if file already exists and returns if yes.
    if(file_exists(dirc, get_block(disk, gd -> bg_inode_table, BLOCK_SIZE), dirName)==0){
        fprintf(stderr, "rm: File does not exist\n");
        return ENOENT;
    }
    //Getting info to check that destination is a directory and not reserved
    struct basic_fileinfo file_info = find_file(pth, get_block(disk, gd -> bg_inode_table, BLOCK_SIZE));
    if(file_info.inode < 11 && file_info.inode != 2){
        fprintf(stderr, "Invalid destination");
        return ENOENT;
    }
	
	file_info = find_file(argv[2], get_block(disk, gd -> bg_inode_table, BLOCK_SIZE));
	if(file_info.type == 'd'){
        fprintf(stderr, "rm: Cant delete a directory");
        return EISDIR;
    }
    //gets inode of file to be deleted.
    int inode_num = file_info.inode;
    struct ext2_inode *current_file_inode = get_inode(get_block(disk, gd -> bg_inode_table, BLOCK_SIZE), inode_num);
    // Getting parent directory
    struct basic_fileinfo parent_info = find_file(pth, get_block(disk, gd -> bg_inode_table, BLOCK_SIZE));
    inode_num = parent_info.inode;
    char *filename = extract_filename(argv[2]);
    char *inode_base = get_block(disk, gd->bg_inode_table, BLOCK_SIZE);
	
    // Loops through the iblocks of parent directory in search for the directory
    // entry to be deleted.
    struct ext2_inode *current_inode = get_inode(inode_base, inode_num);
    unsigned int block_num = current_inode->i_block[0];
    
    unsigned int cur_index_serviced = 0, cur_inode_index = 0;
    char *current_block = get_block(disk, block_num, BLOCK_SIZE);
    struct ext2_dir_entry_2 *current = (struct ext2_dir_entry_2 *) current_block;
    struct ext2_dir_entry_2 *previous = NULL;
    while(cur_index_serviced < current_inode -> i_size){
        current = (struct ext2_dir_entry_2 *) (current_block + (cur_index_serviced % BLOCK_SIZE));
        // Checks if current entry is the one to be deleted(name match)
        if (strlen(filename) == current -> name_len && !strncmp(filename, current -> name, strlen(filename))){
		    //If there is no previous entry, two cases exist.
            if(previous==NULL){
				//If the current directory entry takes up the entire block, "free" block by unsetting the corresponding bit in bitmap.
				//and "shift" the i_block numbers down an index including the indirect blocks.
                if(current->rec_len==BLOCK_SIZE){
                    //unset block and move blocks after by 1.
                    unsigned int cur_index = cur_inode_index;
                    unsigned int blocks_used = (current_inode -> i_size > 0)? ((current_inode -> i_size - 1)/BLOCK_SIZE) + 1 : 0;
                    //Loops through all blocks coming up and shifts
					if(cur_inode_index < 12){
						unset_bit(get_block(disk, gd->bg_block_bitmap, BLOCK_SIZE), current_inode -> i_block[cur_inode_index] - 1);
						gd -> bg_free_blocks_count++;
					}
					else{
                        //Gets indirect block and unsets bit of corresponding element in that block
						unsigned int *indirect = (unsigned int*) get_block(disk, current_inode -> i_block[12], BLOCK_SIZE);
						unset_bit(get_block(disk, gd->bg_block_bitmap, BLOCK_SIZE), indirect[cur_inode_index - 12] - 1);
					}
                    //Loops through all blocks coming up
                    while(cur_index < blocks_used-1){
                        unsigned int* indirect_block;
                        //We are on the 11th iblock
                        if(cur_index==11){
                            //Implies 12th block is being used does the shift
							indirect_block = (unsigned int*)get_block(disk, current_inode->i_block[cur_index+1], BLOCK_SIZE);
                            current_inode->i_block[cur_index]=indirect_block[0];
							//if indirect block is being used
                            if(cur_index+1 < blocks_used-1){
								unset_bit(get_block(disk, gd->bg_block_bitmap, BLOCK_SIZE), current_inode -> i_block[12] - 1);
								current_inode -> i_blocks -= 2;
								//IN THIS CASE FREE THE BLOCK BY UNSETTING THE I_BLOCK[12]TH BIT IN THE BITMAP
                            }
                        }else{
                            //If entry is in indirect block complete the shift within the indirect block.
                            if(cur_index>=12){
                                indirect_block[cur_index - 12] = indirect_block[cur_index - 11];
                            }
							else{
								current_inode->i_block[cur_index] = current_inode->i_block[cur_index+1];
							}
                        }
						// Increment curr index
						cur_index++;
                    }
                    current_inode -> i_size -= BLOCK_SIZE;
                    current_inode -> i_blocks -= 2;
				}
			    else{
				    //Copy next element in place of current and increment length.
					//Setting previous to next element
					unsigned short current_reclen = current -> rec_len;
					//AND NEXT NOW REPRESENTS NEXT
					struct ext2_dir_entry_2 *next = (struct ext2_dir_entry_2 *) (current_block + ((cur_index_serviced + current -> rec_len) % BLOCK_SIZE));
					memcpy(current, next, next->rec_len); //Note previous is now the next element
					current -> rec_len += current_reclen;
                }
			}else{
                    //Not first
                    previous->rec_len += current->rec_len;
            }
			break;
        }
        //Sets the previous element to current and incremenets cur index
        previous = current;
        cur_index_serviced += current -> rec_len;
        if (cur_index_serviced < current_inode->i_size && cur_index_serviced % BLOCK_SIZE == 0){
            cur_inode_index++;
            current_block = get_block_from_inode(inode_base, inode_num, cur_inode_index);
            previous=NULL;
        }
    }
    
    //unsets all block bits
    char *bitmaprs = get_block(disk, gd->bg_block_bitmap, BLOCK_SIZE);
    char *bitmapind = get_block(disk, gd->bg_inode_bitmap, BLOCK_SIZE);
    // Decrements i_link_count to make sure it is 0 else we cant
    // unset as there is still a hard link pointing to it.
	current_file_inode -> i_links_count --;
    // unset inode(unset_bit) if no hard links and unset all block bits(free_inode helper)
	if(current_file_inode -> i_links_count == 0){
		gd -> bg_free_inodes_count++;
		gd -> bg_free_blocks_count += current_file_inode -> i_blocks / 2;
		free_inode(current_file_inode, disk, bitmaprs);
		unset_bit(bitmapind, file_info.inode - 1);
	}
	printf("File removed.");
    return 0;
    
}
