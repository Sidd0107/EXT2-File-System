#define BLOCK_SIZE 1024

struct basic_fileinfo{
	char type; //need to communicate if regular file or directory
	unsigned int inode;
};

extern char *combine_path_and_file(char *path, char *file_name);
extern int file_exists(char *path, char *inode_base, char *file_name);
extern struct basic_fileinfo find_file(char *path, char *inode_base);
extern struct parsed_filename *parse_directory(char *path);
extern struct basic_fileinfo search_directory(char *inode_base, unsigned int inode_num, struct parsed_filename *filename);
extern char type_to_char(unsigned char type);
extern char get_bit(char *bitmap, int bit);
extern char *get_block(void *disk, unsigned int block_num, size_t block_size);
extern struct ext2_inode *get_inode(void *inode_base, unsigned int inode_number);
extern char * get_block_from_inode(char *inode_base, unsigned int inode_num, unsigned int inode_index);
extern char get_bit(char *bitmap, int bit);
extern int find_first_free_bit(char *bitmap, int startbit, int bitmap_num_bits);
extern int find_last_free_bit(char *bitmap, int num_bits);
extern void set_bit(char *bitmap, int bit);
extern void unset_bit(char *bitmap, int bit);
extern int reserve_directory_entry(char *inode_base, unsigned int inode_num, int num_free_blocks, struct ext2_dir_entry_2 *new_file, char *block_bitmap);
extern int add_block_in_inode(struct ext2_inode *current_inode, char *block_bitmap, unsigned int inode_num);
extern void set_block_in_inode(struct ext2_inode* inode, unsigned block_number, unsigned inode_index, char *block_bitmap);
extern void set_inode(struct ext2_inode *inode, unsigned int inode_num, unsigned int size, unsigned *blocks, int num_blocks, char *block_bitmap, char *inode_bitmap, unsigned int mode);
extern char *extract_filename(char *filename);
extern void print_directory_contents(char *inode_base, unsigned int inode_num);
extern void free_inode(struct ext2_inode *current_inode, void *disk, char *blockbitmap);
