all:boot.bin kernel.elf ../cmd/dimg
#'make' will search target-file and rely-file according through VPATH,if she
#failed under current directory.
VPATH=../lib/ ../bin/
#header_path will be used when 'gcc -M'.you can not use '$(cflags)' instead of
#'$(head_path)' under this case,because other unrelated cflags may affect the 
#operation of 'gcc -M',according to by test.
cheader_path=-I../include/ -I./usr/include/
cflags=$(cheader_path) -Wall -std=c99 -fno-builtin -g 
asmflags=-i../include/ -i./usr/include/
ldpath=-L../lib/

#usro= usr/lib/asmtask.o usr/lib/sys_call.o usr/lib/utils.o
#asmo=../lib/kernel_asm.o ../lib/proc_asm.o ../lib/utils_asm.o
usro= ../src/usr/lib/asmtask.o ../src/usr/lib/sys_call.o ../src/usr/lib/utils.o
#the target of a rule matchs in a stiff way,kernel.a will match '%.a:%.asm' but
#../lib/kernel.a won't.
asmo=kernel.asmo proc.asmo utils.asmo
co= kernel.o   utils.o proc.o kbd_drv.o  func_table.o disp.o video_drv.o\
ku_utils.o hs.o  fs_ext.o struinfo.o  mm.o fs.o tty.o
kernelrelyo=$(asmo) $(usro)  $(co)
include $(co:.o=.d)

boot.bin:boot.asm
	nasm -o boot.bin boot.asm
kernel.elf:$(kernelrelyo)
	cd ../lib/;\
	ld -m elf_i386 -s -Ttext 0x30400 -o ../bin/kernel.elf $(kernelrelyo);\
	ld -m elf_i386 -Ttext 0x30400 -o ../bin/g_kernel.elf $(kernelrelyo) 
../cmd/dimg:../cmd/dimg.c
	gcc -o  ../cmd/dimg ../cmd/dimg.c -std=c99
%.d:%.c
	set -e;rm -f $@;\
	gcc -M $< $(cheader_path) > this_is_a_tmp_file;\
	sed 's\:\ $@: \'  this_is_a_tmp_file  > $@;\
	rm  -f this_is_a_tmp_file

%.o:%.c
	gcc -c -o ../lib/$@ $< $(cflags)

%.asmo: %.asm ../include/*.inc
	nasm -felf -o ../lib/$@ $< $(asmflags)
clean:
	rm  -f ../lib/*;\
	rm -f *.d;
#replace your 400m.img to after using make git
git:
	rm  -f ../lib/*;rm -f ../bin/*;rm -f *.d;\
	rm -f ../cmd/parport.out ../cmd/dimg;\
	rm -f usr/lib/*;\
	-mv ../cmd/400m.img ../..
.PHONY:all





























