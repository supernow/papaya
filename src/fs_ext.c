//1,inode_table[inode].block_count包含middle-seed block么？你看，12+256+1k的文件t.bin占用272个block
#include<hs.h>
#include<fs.h>
#include<fs_ext.h>
#include<proc.h>
#include<sys_call.h>
#include<disp.h>
#include<struinfo.h>
#include<utils.h>
#include<ku_utils.h>
#include"./usr/include/errno.h"
static int getInodeByName(char*name,int inode_d);
static int getInodeByPath(char*name);
static DIRENT* getDirentByName(char*name,int inode_d);
static void init(void);
static void adjust(void);
static INODE*getInode(int inode_id);
static int loadpart(int inode,char*_load_addr,int seek,int size);
//这些宏是增强fs可读性，不是为了兼容和扩展
#define BLOCK_SIZE (1024<<(sb.s_log_block_size))
#define BLOCK_SECTORS (BLOCK_SIZE/512)
#define BTS(num_block) (num_block*BLOCK_SECTORS)
#define BTL(block_id) (g_dp[currfd.device][currfd.partition].start_lba\
		+BTS(block_id))
#define ROOT_INODE 2
//gdt
#define GDT_SIZE (0x200000)
#define GDT_SECTORS (GDT_SIZE/512)
#define GROUP_NUM ((sb.s_blocks_count-sb.s_first_data_block)\
		/sb.s_blocks_per_group+1)
//inode_table
#define INODE_SIZE 128
#define INODE_PER_SECTOR (512/INODE_SIZE)

#define DIRBLOCKS_SIZE (48*1024)
#define FSBUF_SIZE (100*1024)

static SUPER_BLOCK sb;		/**buffer superblock*/
static char*fsbuf;			/**common buffer for ext*/
static char*dirblocks;		/**temp buffer a dir-type file*/
static INODE*inodesector;	/**temp buffer some inodes*/
static GROUP_DESC*gdt;		/**buffer group-desc table*/
static  FS_COMMAND*pt_cmd;
#define cmd (*pt_cmd)
#define currfd (fd_table[cmd.fd])
static int curr_dp_lba=0; 
void fs_ext(void){
	oprintf("fs_ext init..\n");
	init();//必须
	while(1){
//		oprintf("enter..\n");
		/**check wheather exists file-request*/
		while((pt_cmd=is_there_cmd_wait())==0){
			oprintf("@fs_ext can not find task-cmd now,sleep..\n");
			sleep(MSGTYPE_USR_ASK,0);
		}
		oprintf("fs_ext awaked..\n");
		//if partition changed,adjust to the new partition
		u32 new_dp_lba=g_dp[currfd.device][currfd.partition].start_lba;
		if(new_dp_lba!=curr_dp_lba){
			curr_dp_lba=new_dp_lba;
			adjust();
			oprintf("@fs_ext adjust done,cmd.command=%u\n",cmd.command);
		}
		if(cmd.command==COMMAND_OPEN){
			oprintf("fs_ext do open now\n");
			int inode=getInodeByPath(cmd.shortpath);
			/*here we increment fd's seek*/
			if(inode!=-1){
				oprintf("@fs_ext got inode\n");
				INODE*pinode=getInode(inode);
				fd_table[cmd.fd].inode=inode;
				fd_table[cmd.fd].filesize=pinode->i_size;
				syscall_soft_ret_to(cmd.asker,cmd.fd,ESUCCESS);
			}
			else{
				oprintf("fs_ext error:can not getInodeByPath\n");
				/*k_open failed for bad path,release the burning file_desc*/
				releasefd(cmd.fd);
				syscall_soft_ret_to(cmd.asker,-1,ENOENT);
			}
		}
		else if(cmd.command==COMMAND_READ){
			int r_bytes=loadpart(currfd.inode,cmd.addr,currfd.seek,cmd.size);
			if(r_bytes==-1) syscall_soft_ret_to(cmd.asker,r_bytes,EUNDEF);
			else syscall_soft_ret_to(cmd.asker,r_bytes,ESUCCESS);
		}
		/**a read-only filesystem now*/
		else if(cmd.command==COMMAND_WRITE){
			syscall_soft_ret_to(cmd.asker,-1,EUNDEF);
		}
		else{
			assert(0)
		}
		/**done,reset the command*/
		cmd.command=COMMAND_NULL;
	}
}



/**you give me an inode id,i will load it and return you the pointer
 * this pointer will be invalid after you call another getInode()
 */
static INODE*getInode(int inode_id){
	//inode 位于哪个组，位于在组内的id？
	oprintf("@getInode start\n");
	int real_inode_id=inode_id-1;
	int group=real_inode_id/sb.s_inodes_per_group;
	int id=real_inode_id%sb.s_inodes_per_group;

	//不能将整个inode_table加载进来，看inode位于哪个sector?
	int sector_offset=id/INODE_PER_SECTOR;//which sector contains the target-inode
	int id_within_sector=id%INODE_PER_SECTOR;
	u32 inode_table_lba=BTL(gdt[group].bg_inode_table);
	askhs(COMMAND_READ,inode_table_lba+sector_offset,1,(char*)inodesector);
	return &inodesector[id_within_sector];
}

static void adjust(void){
	askhs(COMMAND_READ,BTL(0)+2,2,(char*)&sb);//buffer super block
	oprintf("block_size:%u,block_count:%u,magic:%x\n",\
			BLOCK_SIZE,sb.s_blocks_count,sb.s_magic);
	oprintf("group_num:%u,gdt_sectors:%u\n",GROUP_NUM,\
			ceil_divide(GROUP_NUM*sizeof(GROUP_DESC),512));
	/*(1kb)0:mbr,1:super block,2:gdt  (2kb,4kb)0:mbr+super block,1,gdt*/
	int gdt_block=(sb.s_log_block_size==0?2:1);
	askhs(COMMAND_READ,BTL(gdt_block),\
		  ceil_divide(GROUP_NUM*sizeof(GROUP_DESC),512),(char*)gdt);//buffer gdt
}

/**1,this function is for internal use of 'init_g_dp',and it is a recursive
 * function.
 * 2,it traverses the dpt of all partitions,and register the partition
 * information to 'g_dp[]'.
 */
static void init_g_dp_extend(u32 ept_lba,u32 extend_start){
	static int logic_id=5;
	askhs(COMMAND_READ,ept_lba,1,fsbuf);
	DP*dp=(DP*)((char*)fsbuf+DPT_OFFSET);
	memcp((char*)(&g_dp[1][logic_id]),(char*)dp,16);
	g_dp[1][logic_id].start_lba=dp->start_lba+ept_lba;
	assert(dp[0].start_lba!=0);//even a pure extend-dp has one ept-entry
	oprintf("%10u%10u%10u%10u%10u%10s\n",logic_id,dp->boot,\
			(dp->start_lba+ept_lba),dp->count,dp->sys_id,sys_string[dp->sys_id]);
	logic_id++;
	if(dp[1].start_lba!=0)	init_g_dp_extend(extend_start+dp[1].start_lba,\
			extend_start);
}

/**here is why fs_ext should run ahead of fs_fat32/ntfs*/
static void init_g_dp(void){
	oprintf("\n%10s%10s%10s%10s%10s%10s\n","device","boot","start","count",\
			"sys_id","sys_string");	
	u32 extend_start=-1;
	askhs(COMMAND_READ,0,1,fsbuf);
	DP*dp=(DP*)((char*)fsbuf+DPT_OFFSET);
	memcp((char*)(&g_dp[1][1]),(char*)dp,64);
	for(int i=1;i<5;i++){
		if(dp->start_lba==0) break;/**meet an empty dp,end info*/
		/**better:init_g_dp_extend can be set here*/
		if(dp->sys_id==SYSID_EXTEND) extend_start=dp->start_lba;
		oprintf("%10u%10u%10u%10u%10u%10s\n",i,dp->boot,dp->start_lba,dp->count,\
				dp->sys_id,sys_string[dp->sys_id]);
		dp++;
	}
	if(extend_start!=-1)  init_g_dp_extend(extend_start,extend_start);
}

/**1 search a file-name under a directory,and return the pointer to corresponding
 * directory entry.
 * 2 the pointer will be invalid after you call another 'getDirentByName'.
*/
DIRENT* getDirentByName(char*name,int inode_d){//不能接受空字符串
	oprintf("@%s run,i want to get inode_d:%u\n",__func__,inode_d);
	INODE*pinode=getInode(inode_d);
	oprintf("getInode done \n");
	assert((pinode->i_mode>>12)==INODE_FILE_DIR)
	/**将目录的block内容(即dirent序列)加载到dirblocks*/
	loadpart(inode_d,dirblocks,0,DIRBLOCKS_SIZE);
	DIRENT*dirent=(DIRENT*)(dirblocks);
	int offset=0;
	while(dirent->record_len!=0){//若记录长度为0,视为表项遍历完了
		oprintf("entry name:%*s\n",dirent->record_len,dirent->name);
		if(dirent->inode==0) goto next_dirent;//该文件项已删除
		int i;
		for(i=0;i<dirent->name_len;i++){
			if(dirent->name[i]!=name[i]) goto next_dirent;//文件名不匹配
		}
		//now,i=dirent->name_len
		if(name[i]!='\0'&&name[i]!='/'){
			oprintf("name only match with name_len,the arg-name had exceeding\
					chars,next dirent..\n");
			goto next_dirent;
		}
//		oprintf("name matched..now,test type\n");
		if(name[i]=='\0'){
			if(dirent->file_type!=BLOCK_FILE_REGULAR){
				oprintf("type test failed,seg-name require regular file\n");
				goto next_dirent;//文件类型不匹配
			}
//			oprintf("type test success,seg-name require regular file\n");
			return dirent;
		}
		else if(name[i]=='/'){
			if(dirent->file_type!=BLOCK_FILE_DIR){
				oprintf("type test failed,seg-name require dir\n");
				goto next_dirent;
			}
			return dirent;
		}
		else{
			assert(0)
		}
		next_dirent:
			offset+=dirent->record_len;
			dirent=(DIRENT*)(dirblocks+offset);
	}
	return 0;
}

/**1 a long function,but easy to read
 * 2 load a part(may be whole part) of a file.
 * @return
 * -1 seek behind the tail of file
 *  >=0 bytes readed
 */
static int loadpart(int inode_id,char* _load_addr,int seek,int size){
	INODE* pinode=getInode(inode_id);
	oprintf("@loadpart begin..currseek:%u,filesize:%u,cmd.filesize:%u\n",seek,\
			pinode->i_size,size);
	if(seek>=pinode->i_size) return -1;			//you can't read behind tail
	int realsize=min(size,pinode->i_size-seek);	//fix cmd.size if out of filesize

	/**we will read this file from [startseek]byte to [endseek]byte.*/
	int startseek=seek;
	int endseek=seek+realsize-1;

	int start_block=startseek/BLOCK_SIZE;
	int end_block=endseek/BLOCK_SIZE;	
	int seek_in_startblock=startseek%BLOCK_SIZE;
	int seek_in_endblock=endseek%BLOCK_SIZE;
	oprintf("startseek:%u,endseek:%u,start_block:%u,end_block:%u,\
			seek_in_startblock:%u,seek_in_endblock:%u\n",startseek,endseek,\
			start_block,end_block,seek_in_startblock,seek_in_endblock);
	
	/*convert physical sector-count to block-count*/
	int block_remain=pinode->block_count/BLOCK_SECTORS;
	oprintf("when i start reading block,block_remain=%u\n",block_remain);
	int i,j,k;
	int curr_block=0;
	for(i=0;i<12;i++){
		if(block_remain<=0||curr_block>end_block) return realsize;
		if(curr_block>=start_block){
			/**必须先判断是不是start_block，因为start_block可能=end_block*/
			if(curr_block==start_block){
				askhs(COMMAND_READ,BTL(pinode->blocks[i]),BLOCK_SECTORS,fsbuf);
				int datasize=min(BLOCK_SIZE-seek_in_startblock,realsize);
				memcp(_load_addr,fsbuf+seek_in_startblock,datasize);	
				_load_addr+=datasize;
			}
			else if(curr_block==end_block){
				askhs(COMMAND_READ,BTL(pinode->blocks[i]),BLOCK_SECTORS,fsbuf);
				memcp(_load_addr,fsbuf,seek_in_endblock+1);	
				/*_load_addr+=?		end now!*/
			}
			else{
				askhs(COMMAND_READ,BTL(pinode->blocks[i]),BLOCK_SECTORS,\
						_load_addr);
				_load_addr+=BLOCK_SIZE;
			}
		}
		oprintf("block field 1..\n");
		curr_block++;
		block_remain--;
	}

	oprintf("goto block_field2 now,block_remain:%u\n",block_remain);
	int id_table1[BLOCK_SIZE/4];
	askhs(COMMAND_READ,BTL(pinode->blocks[12]),BLOCK_SECTORS,(char*)id_table1);
	block_remain--;
	for(i=0;i<BLOCK_SIZE/4;i++){
		if(block_remain<=0||curr_block>end_block) return realsize;
		if(curr_block>=start_block){
			if(curr_block==start_block){
				askhs(COMMAND_READ,BTL(id_table1[i]),BLOCK_SECTORS,fsbuf);
				int datasize=min(BLOCK_SIZE-seek_in_startblock,realsize);
				memcp(_load_addr,fsbuf+seek_in_startblock,datasize);	
				_load_addr+=datasize;
			}
			else if(curr_block==end_block){
				askhs(COMMAND_READ,BTL(id_table1[i]),BLOCK_SECTORS,fsbuf);
				memcp(_load_addr,fsbuf,seek_in_endblock+1);	
			}
			else{
				askhs(COMMAND_READ,BTL(id_table1[i]),BLOCK_SECTORS,_load_addr);
				_load_addr+=BLOCK_SIZE;
			}
		}
		oprintf("block field 1..\n");
		curr_block++;
		block_remain--;
	}

	oprintf("goto block_field3 now,block_remain:%u\n",block_remain);
	int id_table2[BLOCK_SIZE/4];
	askhs(COMMAND_READ,BTL(pinode->blocks[13]),BLOCK_SECTORS,(char*)id_table1);
	block_remain--;
	for(i=0;i<BLOCK_SIZE/4;i++){
		askhs(COMMAND_READ,BTL(id_table1[i]),BLOCK_SECTORS,(char*)id_table2);	
		block_remain--;
		for(j=0;j<BLOCK_SIZE/4;j++){
			if(block_remain<=0||curr_block>end_block) return realsize;
			if(curr_block>=start_block){
				if(curr_block==start_block){
					askhs(COMMAND_READ,BTL(id_table2[i]),BLOCK_SECTORS,fsbuf);
					int datasize=min(BLOCK_SIZE-seek_in_startblock,realsize);
					memcp(_load_addr,fsbuf+seek_in_startblock,datasize);	
					_load_addr+=datasize;
				}
				else if(curr_block==end_block){
					askhs(COMMAND_READ,BTL(id_table2[i]),BLOCK_SECTORS,fsbuf);
					memcp(_load_addr,fsbuf,seek_in_endblock+1);	
				}
				else{
					askhs(COMMAND_READ,BTL(id_table2[i]),BLOCK_SECTORS,_load_addr);
					_load_addr+=BLOCK_SIZE;
				}
			}
		}
	}
	assert(block_remain<0)
	return 0;//ask commpiler shut up!
	//regular file lager than 64M is not welcome under papaya,so...
}
void init(void){
	fsbuf=(char*)kmalloc(FSBUF_SIZE);	
	gdt=(GROUP_DESC*)kmalloc(GDT_SIZE);
	inodesector=(INODE*)kmalloc(512);
	dirblocks=(char*)kmalloc(DIRBLOCKS_SIZE);
	oprintf("@fs identify hard disk..\n");
	askhs(COMMAND_IDENTIFY,1,1,(char*)1);

	for(int i=0;i<256;i++) sys_string[i]="unknown";
	sys_string[0]="empty";
	sys_string[0x5]="extend";
	sys_string[0x83]="linux";
	sys_string[0x6]="fat16";
	sys_string[0x7]="hpfs/ntfs";
	init_g_dp();
}

/**you give me a path,i will return you a corresponding inode*/
int getInodeByPath(char*path){
	char*pt=path;
	if(*pt!='/') return -1;
	int inode_d=ROOT_INODE;
	pt++;//jump fist /
	while(*pt!=0){		
		/**every time the loop begins,pt is pointing at a 'name' such as 'home',
		 * 'mnt',etc.we should make sure the 'name' is not '\0'*/
		inode_d=getInodeByName(pt,inode_d);	/**inode_d可能不再是目录inode，而是
											  文件inode。*/
		if(inode_d==-1) returnx_say(-1,"@gInodeBPath:getInodeByName ret -1\n");
		while(*pt!=0&&*pt!='/') pt++;//跳到下一个分割标志
		if(*pt==0)	break;//meet path tail
		else		pt++;//point to next name
	}
	return inode_d;
}

/**for internal use by getInodeByPath*/
int getInodeByName(char*name,int inode_d){//不能接受空字符串
	oprintf("@getInodeByName:run\n\n");
	DIRENT*dirent=getDirentByName(name,inode_d);
	if(!dirent) return -1;
	return dirent->inode;
}

/**
void loadFileByInode(int inode_id,char*_load_addr){
	INODE* pinode=getInode(inode_id);
	int block_remain=pinode->block_count/BLOCK_SECTORS;//convert physical sector-count to block-count
	oprintf("@loadFileByInode block_remain:%u\n",block_remain);
	int i,j,k;
	for(i=0;i<12;i++){
		if(block_remain<=0) return;
		oprintf("block field 1..\n");
		askhs(COMMAND_READ,BTL(pinode->blocks[i]),BLOCK_SECTORS,_load_addr);
		_load_addr+=BLOCK_SIZE;
		block_remain--;
	}

	oprintf("goto block_field2 now,block_remain:%u\n",block_remain);
	int id_table1[BLOCK_SIZE/4];
	askhs(COMMAND_READ,BTL(pinode->blocks[12]),BLOCK_SECTORS,(char*)id_table1);
	block_remain--;
	for(i=0;i<BLOCK_SIZE/4;i++){
		if(block_remain<=0) return;
		oprintf("block field 2..\n");
		askhs(COMMAND_READ,BTL(id_table1[i]),BLOCK_SECTORS,_load_addr);
		block_remain--;
		_load_addr+=BLOCK_SIZE;
	}

	oprintf("goto block_field3 now,block_remain:%u\n",block_remain);
	int id_table2[BLOCK_SIZE/4];
	askhs(COMMAND_READ,BTL(pinode->blocks[13]),BLOCK_SECTORS,(char*)id_table1);
	block_remain--;
	for(i=0;i<BLOCK_SIZE/4;i++){
		askhs(COMMAND_READ,BTL(id_table1[i]),BLOCK_SECTORS,(char*)id_table2);	
		block_remain--;
		for(j=0;j<BLOCK_SIZE/4;j++){
			if(block_remain<=0) return;
			askhs(COMMAND_READ,BTL(id_table2[j]),BLOCK_SECTORS,_load_addr);
			_load_addr+=BLOCK_SIZE;
			block_remain--;
		}
	}
	assert(block_remain<0)
	//regular file lager than 64M is not welcome under papaya,so...
}
*/
