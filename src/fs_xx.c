/**1 to write a filesystem that implemens posix,you should first know well
 * abount open(),read(),close() and lseek()
 * 2 this is only a frame of fs_ext,but you may use it  for reference.
 */
#include<hs.h>
#include<fs.h>
#include<fs_xx.h>
#include<proc.h>
#include<sys_call.h>
#include<disp.h>
#include<struinfo.h>
#include<utils.h>
#include<ku_utils.h>
#define FSBUF_SIZE (100*1024)

static  FS_COMMAND*pt_cmd;
#define cmd (*pt_cmd)
#define currfd (fd_table[cmd.fd])
void fs_xx(void){
	init();
	while(1){
		while((pt_cmd=is_there_cmd_wait())==0){
			sleep(MSGTYPE_USR_ASK,0);
		}

		/**
		 * 1 fs-layer already creates a file-desc and fs_xx layer should
		 * try to finish the initialization of the file_desc.
		 */
		if(cmd.command==COMMAND_OPEN){
			/**'getInodeByPath' is a function under fs_ext*/
			int inode=getInodeByPath(cmd.shortpath);
			if(inode!=-1){
			/**'getInode' is a function under fs_ext,it receives a inode number
			 * returns a pointer to the specified inode.
			 */ 
				INODE*pinode=getInode(inode);
				fd_table[cmd.fd].inode=inode;
				fd_table[cmd.fd].filesize=pinode->i_size;
				/**this macro is commented in proc.h*/
				SYSCALL_SOFT_RET_TO(cmd.asker,cmd.fd,0);
			}
			else{
				/**k_open failed,release the burning file_desc*/
				releasefd(cmd.fd);
				SYSCALL_SOFT_RET_TO(cmd.asker,-1,0xff);
			}
		}

		else if(cmd.command==COMMAND_READ){
			/**'loadpart' is a function under fs_ext,it parses the file_desc
			 * and load the specifed part(offset,offset+len) of file to a
			 * specified buffer.
			 */
			int r_bytes=loadpart();
			SYSCALL_SOFT_RET_TO(cmd.asker,r_bytes,0xff);
		}

		/**read-only filesystem,ignore this branch*/
		else if(cmd.command==COMMAND_WRITE){
			SYSCALL_SOFT_RET_TO(cmd.asker,-1,0xff);
		}
		else{
			assert(0)
		}
		/**we have finish handling a cmd,renew it*/
		cmd.command=COMMAND_NULL;
	}
}
void init(void){
}
