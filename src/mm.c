//1~256M空间全部做了对等映射，但内核只用到2M，似乎浪费
#include<disp.h>
#include<utils.h>
#include<mm.h>
#include<valType.h>
#include<proc.h>
/**mm.c maintains a page-bitmap*/
static u8 *pgbmp;
static void check_dirent(u32*dir,int entry_id);
static void set_pgbmp(int start_page,int pages);
static void init(void);
void mm(void){
	oprintf("mm init..\n");
	init();
	while(1){
	}
}

/**
 * page-error exception handler
 * alloc a physical page and mapped it to the ill virtual page
 */
void pgerr(void){
	u32 err_addr;
	__asm__ __volatile__(
			"movl %%cr2,%0"
			:"=r" (err_addr)
			:
			:
	);
	u32 ppg=alloc_page();
	oprintf("pgerr solved,err_addr:%x,ppg alloc:%x,ill proc continue..\n",err_addr,ppg);
//	oprintf("proc_get_ppg:%x\n",proc_get_ppg(pcb_table_info.curr_pid,err_addr>>12));
	proc_map_pg(pcb_table_info.curr_pid,err_addr>>12,ppg,PG_USU,PG_RWW);
	fire(pcb_table_info.curr_pid);
}

/**if page-directory has a certain empty entry,then make this entry point to a empty page table*/
static void check_dirent(u32*dir,int entry_id){
//dir-entry都是用户级别,存在，可写，身份识别统统放在table-entry
	if((dir[entry_id]&PG_P)==0){
		int ppg_id=alloc_page();
		dir[entry_id]=PG_RWW|PG_USU|PG_P|ppg_id<<12;
//		oprintf("meet empty dir-entry,alloc a physical page for linking table,ppg_id=%x\n",ppg_id);
	}
}

/**give me a page-directory,give me the virtual-page id,i will tell you the
 * mapped physical-page id
 */
int get_ppg(u32*dir,int vpg_id){
	u32*tbl=(u32*)(dir[PG_H10(vpg_id)]>>12<<12);
	oprintf("@get_ppg tbl_addr:%x\n",tbl);
	return tbl[PG_L10(vpg_id)]>>12;
}

/**just a wrapper of get_ppg(),because as we know pid,we know it's page-directory
 * address.
 * TODO replace it with a macro?*/
int proc_get_ppg(int pid,int vpg_id){
	u32*dir=(u32*)ADDR_PROC_PGDIR(pid);
	return get_ppg(dir,vpg_id);
}

/**under papaya,stack room for every process is pre-alloated,just map STACK_PAGE
 * to process's  virtual space*/
void proc_map_stackpg(int pid){
	for(int i=0;i<PROC_STACK_PGS;i++){
		int stack_pg=PROCSTACK_STARTPG(pid)+i;
		proc_map_pg(pid,stack_pg,stack_pg,PG_USU,PG_RWW);
	}
}

/**equal map for kernel spcace*/
void proc_map_kpg(int pid){		/**ERR 暂时的kernel page都是用户可读写的*/
	for(int pg_id=0;pg_id<KERNEL_SPACE_SIZE/PGSIZE;pg_id++){
		proc_map_pg(pid,pg_id,pg_id,PG_USS,PG_RWW);
	}
}


void proc_map_pg(int pid,int vpg_id,int ppg_id,int us,int rw){
	u32*dir=(u32*)ADDR_PROC_PGDIR(pid);
//	oprintf("@proc_map_pg,dir:%x\n",dir);
	map_pg(dir,vpg_id,ppg_id,us,rw);
}

/**equal map for the whole memory space of a machine*/
void global_equal_map(void){
	u32*entry=(u32*)RING0_PGDIR;		
	for(int i=0;i<1024;i++){
		*entry=(RING0_PGTBL+i*0x1000)>>12<<12|PG_USS|PG_RWW|PG_P;//i thought '&0x1000' the same as '>>12<<12' at first,and debug a long time
		entry++;
	}
	entry=(u32*)RING0_PGTBL;
	for(int pg_id=0;pg_id<PAGES;pg_id++){
		*entry=pg_id<<12|PG_USS|PG_RWW|PG_P;
		entry++;
	}
}
void map_pg(u32*dir,int vpg_id,int ppg_id,int us,int rw){
//	CHECK_DIRENT(dir,PG_H10(vpg_id));
	check_dirent(dir,PG_H10(vpg_id));
	u32*tbl=(u32*)(dir[PG_H10(vpg_id)]>>12<<12);
//	oprintf("@map_pg page-table at %x ",tbl);
	tbl[PG_L10(vpg_id)]=ppg_id<<12|us|rw|PG_P;
}
/**
 * |---kernel(1M)---|---kernel pgdir(4k)---|---kernel pgtbl(64*4k)---|---100 pgdir(100*4k)---|
 */
static void init(void){
	pgbmp=kmalloc(PAGES/8);	
	set_pgbmp(0,KERNEL_SPACE_SIZE/PGSIZE);

}
/**scan the page-bitmap and find an empty physical page*/
int alloc_page(){
	int page_id=bt0(pgbmp,PAGES);
	assert(page_id!=-1)
	set_pgbmp(page_id,1);
//	oprintf("alloc_page return %x ",page_id);
	return page_id;
}

/**if a page was allocated,we set the corresponding bit in page-bitmap*/
static void set_pgbmp(int start_page,int pages){
	for(int offset=0;offset<pages;offset++){
		bs(pgbmp,start_page+offset);
	}
}
/**we don't have clear_pgbmp() now,for we don't need such operation for a long 
 * time,we have no energy to release related resource
 */
