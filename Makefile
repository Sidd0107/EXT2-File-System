
all: cp ls mkdir rm ln


cp: a3helper.o ext2_cp.o
	gcc -Wall -g -o ext2_cp $^

ls: a3helper.o ext2_ls.o
	gcc -Wall -g -o ext2_ls $^

mkdir: a3helper.o ext2_mkdir.o
	gcc -Wall -g -o ext2_mkdir $^

rm: a3helper.o ext2_rm.o
	gcc -Wall -g -o ext2_rm $^

ln: a3helper.o ext2_ln.o
	gcc -Wall -g -o ext2_ln $^

%.o : %.c ext2.h
	gcc -Wall -g -c $<

clean :
	rm -f *.o ext2_cp ext2_ls ext2_mkdir ext2_rm ext2_ln
