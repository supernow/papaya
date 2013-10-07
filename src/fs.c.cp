/**
  *why we need a fs layer?
  *1  it is a virtual layer of different filesystem,and it will solve common filesystem requests.
  *2  if needed,it will make the choice which filesystem driver to call
  *3  sometimes,it communicates with fs_xx-layer to finish one job,we take open() as example:fs creates a file-desc and do some initialization,
  *and let fs_xx-layer finish the remaining job.
  */
#include<fs.h>
#include<fs_ext.h>
#include<ku_utils.h>
#include<disp.h>
#include<proc.h>
#include<errno.h>
#define MAX_CMD 10


DP g_dp[MAX_DEVICE][MAX_PARTATION];
FILE_DESC fd_table[MAX_FD];

static FS_COMMAND empty_cmds[MAX_CMD];//use to store unfinished fs req 
#define MAX_MOUNT_INFO 20

/*all mount point infomation is maintained here */
static MOUNT_INFO mountinfo[MAX_MOUNT_INFO];
char* sys_string[256];

/*
1,receive massive arguments,because it will be called by k_open,k_read,k_write,k_colse.
2,choose which filesystem  to call by parsing fd
3,if COMMAND_OPEN,argument 'fd' will be pre-set,as you see within body of if(command==COMMAND_OPEN),that 'fd=new fd'
*/

int askfs(int command,
		char* path,int flags,					/**open(char*path,int flag,int mod); */
		int fd,char*addr,int size,			/**write/read(int fd,char*addr,int size); */
		int offset,int whence							/**lseek(int fd,int offset,int whence)*/
		){
	/*
	 *FIXME:maybe fd should be validated first ? 
	 *'askfs' is designed to be called by several functions,the caller just need to pass a part of arguments and leave the rest zero.
	 *so 'fd' can be zero,see 'func_table.c>>void k_open()>>askfs'.we can't expect k_open pass a valid 'fd' when calling askfs.
	 * */
    if(false == is_fd_valid(fd,command) ){
	    oprintf("kernel error:@ fd not valid\n");
	    SYSCALL_RET(-1,EUNDEF);
	}

	FS_COMMAND*cmd=new_cmd();/*allocate a new fs sys-call*/

	if(!cmd){
		oprintf("kernel error:@fs empty_cmds used out,fs sub-system might be busy\n");
		SYSCALL_RET(-1,EUNDEF);
	}
	if(command==COMMAND_CLOSE){
		fd_table[fd].device=DEVICE_NULL;
		SYSCALL_RET(0,-1);
	}
	else if(command==COMMAND_SEEK){
		oprintf("@fs handle COMMAND_SEEK..\n");
		int newseek=-1;
		switch(whence){
			case 0: 
				oprintf("case0 cmd.offset=%u\n",offset);
				newseek=offset;
				break;
			case 1:
				newseek=fd_table[fd].seek+offset;
				break;
			case 2:
				newseek=fd_table[fd].filesize-1+offset;
				break;
			default:
				break; /*FIXME: without break, would an endless cycle be generated here when case fall to default?*/
				       //if you pass a bad location-flag,'newseek' will not be touched and keeps being -1
		}

		if(newseek>=0){
			fd_table[fd].seek=newseek;
			oprintf("@fs_ext seek sucess,return now=%u",newseek);
		}

		SYSCALL_RET(newseek,-1);
	}
	else if(command==COMMAND_OPEN){
		/**
		  *what does an 'open' do?		---it create a file-desc for target file.
		  *this job was finished by two steps:
		  *1,at fs-layer(namely fs.c),file-desc's member 'flags','device','partition','seek' are initialized.of course,fs-layer can not get inode.
		  *so she pass shortpath to fs_xx_layer(namely fs_ext.c and so on).
		  *2,at fs_xx_layer,'shortpath' will be used to get 'inode' of file-desc.thus,a good file-desc is created and open() is done.
		  */
		fd=new_fd();
		if(fd==-1){
			oprintf("kernel error:@fs file-desc runs out\n");
			SYSCALL_RET(-1,ENFILE);
		}

		fd_table[fd].flags=flags;
		fd_table[fd].seek=0;
		int i;

		/*validate file path and dispatch 'open' command to lower layer.*/
		for(i=0;i<MAX_MOUNT_INFO;i++){

			oprintf("mountpoint:%s,device:%u,partition:%u\n",
				mountinfo[i].mountpoint,mountinfo[i].device,mountinfo[i].partition);
			if(!strmatch(mountinfo[i].mountpoint,path)) continue;
			fd_table[fd].device=mountinfo[i].device;
			fd_table[fd].partition=mountinfo[i].partition;
			/**pass shortpath to fs_xx,fs-layer can not get inode,where path = mount_point/short_path */
			cmd->shortpath=path+strlen(mountinfo[i].mountpoint);
			oprintf("mountpoint:%s,strlen:%u,pass shortpath:%s\n",
				mountinfo[i].mountpoint,strlen(mountinfo[i].mountpoint),cmd->shortpath);
			break;
		}
		if(i==MAX_MOUNT_INFO){
			oprintf("bad mountpoint of path '%s'\n",path);
			SYSCALL_RET(-1,ENOENT);
		}
	}
	else if(command==COMMAND_READ||command==COMMAND_WRITE){
		cmd->addr=addr;
		cmd->size=size;
	}
	else	assert(0);
	/**cmd's common part init */
	cmd->fd=fd;
	cmd->asker=pcb_table_info.curr_pid;
	cmd->command=command;
	
	FILE_DESC*pfd=fd_table+fd;	

	/*finally , command from userspace comes down here, 
	 * get handled by specific file-system module such as ext/ntfs/fat via g_dp ,
	 * */

	switch(g_dp[pfd->device][pfd->partition].sys_id){
		case SYSID_LINUX:
			cmd->handler_pid=FS_EXT_PID;
			if(pcb_table[FS_EXT_PID].mod==TASKMOD_SLEEP&&pcb_table[FS_EXT_PID].msg_type==MSGTYPE_USR_ASK) SLEEP_ACTIVE(FS_EXT_PID);
			proc_dispatch();
			break;
		case SYSID_FAT16:
			break;
		case SYSID_FAT32:
			break;
		case SYSID_NTFS:
			break;
		case SYSID_EXTEND:
			oprintf("entend partition is not partition..\n");
			return 0;
		default:
			oprintf("askfs:unknown partition partition sys_id...\n");
			return 0;
	}

	/*FIXME:*/
	/*some routines might not return correctly when all work's done*/
	return 0;
	/**we  write 'retun 0' just for cheating the compiler,
	 * the routin will never touch here. 
	 * kernel returns a value to usr-process via 'SYSCALL_RET'*/
}
/*bug:empty_cmds should be all 0 at first,but it turned out not */
void init_fs(void){

	for(int i=0;i<MAX_CMD;i++) empty_cmds[i].command=COMMAND_NULL;

	for(int i = 0; i < MAX_FD ; i++) fd_table[i].device = DEVICE_NULL;

	for(int i = 0; i < MAX_MOUNT_INFO ; i++) mountinfo[i].device = DEVICE_NULL;
}

int new_fd(void){
	int i;
	for(i=0;i<MAX_FD;i++){
		//device==0 indicates a empty-file_desc
		if(fd_table[i].device==DEVICE_NULL) break;
	}
	if(i>=MAX_FD) return -1;
	return i;
}

void releasefd(int fd){
	fd_table[fd].device=DEVICE_NULL;
}

/*function should be called within filesystem*/
FS_COMMAND*is_there_cmd_wait(void){
	int i;
	for(i=0;i<MAX_CMD;i++){
		if(empty_cmds[i].handler_pid==pcb_table_info.curr_pid&&empty_cmds[i].command!=COMMAND_NULL) break;
	}
	if(i==MAX_CMD) return 0;
	return &empty_cmds[i];
}

boolean is_partition_valid(short device , short partition)
{
    if( (device < 1 || device >(MAX_DEVICE-1)) ||
	(partition < 1 || partition > MAX_PARTATION))
	return false;

    return true;
}

/*   we mount device/patition such as /dev/sda1 in Linux, to mountpoint 
 *   mount information is maintained for :(1) file path validation
 *					  (2) Ability to dispatch request to correct file system moudule via g_dp
 *					  (3) TODO: nested mount operation ?
 * */

boolean mount(char*mountpoint,short device,short partition){
	int i;
	//if(device<1||device>4||partition<1||partition>15) return false;
	if(is_partition_valid(device,partition) == false) return false;

	for(i=0;i<MAX_MOUNT_INFO;i++){
		if(mountinfo[i].device!=DEVICE_NULL) continue;
		/*avaliable space found*/
		strcpy(mountinfo[i].mountpoint,mountpoint);
		mountinfo[i].device=device;
		mountinfo[i].partition=partition;
		break;
	}

	/*no room left*/
	if(i==MAX_MOUNT_INFO) {
	    oprintf("error: mount points runs out\n");
	    return false;
	}

	int tail_offset=strlen(mountpoint)-1;
	if(mountpoint[tail_offset]=='/'){
		mountinfo[i].mountpoint[tail_offset]='\0';
		oprintf("warning:mountpoint should never end with a ('/') , the slash is removed,new mountpoing string '%s'\n",
			mountinfo[i].mountpoint);
	}
	return true;
}

FS_COMMAND* new_cmd(void){
	int i;
	for(i=0;i<MAX_CMD;i++)	if(empty_cmds[i].command==COMMAND_NULL) break;
	if(i==MAX_CMD)	return 0;
	return empty_cmds+i;
}

boolean is_fd_valid(int fd, int command)
{
    if(fd < 0)
	return false;
    else if( fd >= MAX_FD)
	return false;
    else if((fd_table[fd].device == DEVICE_NULL) &&
	    (command != COMMAND_OPEN))
	return false;
    
    return true;
}
