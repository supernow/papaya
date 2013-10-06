//进程休眠称为sleep，sleep有多种MSGTYPE
#include<mm.h>
#include "proc.h"
#include<utils.h>
#include<kbd_drv.h>
#include<disp.h>
#include<tty.h>
#include<fs.h>
#include<elf.h>
#include<struinfo.h>
extern int p2,i20h;

/**
 * 'ticks' will '++' every time a system-clock interupt occurs,then,i21h will 
 * call proc_dispatch,but this isn't the only approach.we can force it run just
 * writing "proc_dispatch()". so,the function itself distinguish the two cases
 * by comparing 'ticks' and 'old_ticks',and get the conclusion wheather
 * 'ticks_new' now.
 */
extern u32 ticks;
static u32 old_ticks=0;
static int ticks_new=0; 
int size_stackframe=sizeof(STACK_FRAME);

/**process only run at ring0,ring1,ring3*/
static int selector_plain_d[4]={(int)&selector_plain_d0,(int)&selector_plain_d1,0,(int)&selector_plain_d3};
static int selector_plain_c[4]={(int)&selector_plain_c0,(int)&selector_plain_c1,0,(int)&selector_plain_c3};
int eflags=0x1200;//IOPL=1,STI
static int count_dispatch=0;

/**when creating a usr-process,we need to load the executable file from disk,so we need a buffer*/
static char*loadbuf;
static int proc_init_vspace(int pid,char*exec_file);
static void proc_load_seg(int pid,Elf32_Phdr*ph);
static void proc_init_pcb(int pid_empty,u32 addr,int prio,int time_slice,char*p_name,int ring);

void proc_dispatch(void){
//	oprintf("proc_dispath>>>>>>>>>>>>>>>>>>>\n\n\n\n\n");
	count_dispatch++;
	if(old_ticks<ticks){
		old_ticks=ticks;
		ticks_new=1;
	}
	else{
		ticks_new=0;
	}

	int curr_pid=pcb_table_info.curr_pid;
	pcb_table[curr_pid].time_slice--;

	if(pcb_table[curr_pid].mod==TASKMOD_ACTIVE&&pcb_table[curr_pid].time_slice==0){
		if(pcb_table[curr_pid].mod==TASKMOD_ACTIVE){
			ACTIVE_EXPIRE(curr_pid);
		}
	}
	
	/**FIXME  maybe too slow someday*/
	if(pcb_table_info.num_active==1){
		pcb_table_info.num_active+=pcb_table_info.num_expire;
		pcb_table_info.num_expire=0;
		for(int pid=0;pid<100;pid++){
			pcb_table[pid].time_slice=pcb_table[pid].time_slice_full;
			pcb_table[pid].mod==TASKMOD_EXPIRE?(pcb_table[pid].mod=TASKMOD_ACTIVE):0;
		}
	}

	/**we usually use 'sleep(xx)' to make a process halt for xx seconds,now we support this function here*/
	if(ticks_new){
		for(int pid=0;pid<100;pid++){
			if(pcb_table[pid].mod==TASKMOD_SLEEP&&pcb_table[pid].msg_type==MSGTYPE_TIMER){
				if(pcb_table[pid].msg_bind==0){
					SLEEP_ACTIVE(pid);
				}
				else{
					pcb_table[pid].msg_bind--;
				}
			}
		}
	}
	int pid_ok=pickNext();	
	pcb_table_info.curr_pid=pid_ok;
	/**call a function which never return*/
	fire(pid_ok);
}

/**
 * let a process run
 */
void fire(int pid){
	kinfo.curr_pid=pid;
	PCB*pt_pcb=pcb_table+pid;
	if(pt_pcb->ring==3||pt_pcb->fix_cr3){
		__asm__ __volatile__(
				"movl %0,%%cr3"
				:
				:"r" (pt_pcb->cr3)
				:
		);
	}
	fire_asm((int)pt_pcb);
}


/**
 * choose the process with a highest priviledge.
 */
int pickNext(void){//return  min_prio active process's pid
	int minprio=9;
	int minprio_pid=0;
	for(int pid=0;pid<100;pid++){
		if(pcb_table[pid].mod==TASKMOD_ACTIVE&&pcb_table[pid].prio<=minprio){
			minprio=pcb_table[pid].prio;
			minprio_pid=pid;//record a promising pid
		}
	}
	return minprio_pid;
}

/**
 * just get an empty process-control-block
 */
int getEmpty(void){
	int pid_empty=-1;
	for(int pid=0;pid<100;pid++){
		if(pcb_table[pid].mod==TASKMOD_EMPTY){
			pid_empty=pid;
			break;
		}
	}
	assert(pid_empty!=-1);
	return pid_empty;
}

/**
 * kii a process
 * TODO release related page and update page-bitmap
 */
void kill(int pid){
	pcb_table[pid].mod=TASKMOD_EMPTY;	
}

/**
 * because all kernel-process run in kernel-space and compiled into 'kernel.elf'
 * create a kernel-process is quite easy,we do not need to create
 * virtual space,or load elf-file from disk.
 */
void create_kernel_process(u32 addr,int prio,int time_slice,char*p_name,int ring){
	int pid_empty=getEmpty();
	proc_init_pcb(pid_empty,addr,prio,time_slice,p_name,ring);
}

/**
 * after creating a pcb,you have created a process to a certain degree.as you
 * see in the body of create_kernel_pocess().
 * XXX some initialization could be done unified at the very begining for they
 * won't be touched any more(like ss,ds,es)
 */
static void proc_init_pcb(int pid_empty,u32 addr,int prio,int time_slice,char*p_name,int ring){
	//got pid
	PCB* pt_pcb=pcb_table+pid_empty;
	//fill pt_pcb->regs
	pt_pcb->regs.ss=(selector_plain_d[ring]);
	pt_pcb->regs.esp=BASE_PROCSTACK+LEN_PROCSTACK*(pid_empty+1)-4;//leave out room 4-byte for return_errno
	pt_pcb->regs.eflags=eflags;//IOPL=1,STI
	pt_pcb->regs.cs=(selector_plain_c[ring]);
	pt_pcb->regs.eip=addr;

	pt_pcb->regs.gs=pt_pcb->regs.fs=pt_pcb->regs.es=pt_pcb->regs.ds=(selector_plain_d[ring]);
	//fill other members
	pt_pcb->prio=prio;
	pt_pcb->time_slice=pt_pcb->time_slice_full=time_slice;
	pt_pcb->p_name=p_name;
	pt_pcb->pid=pid_empty;
	pt_pcb->mod=TASKMOD_ACTIVE;
	pt_pcb->ring=ring;
	pt_pcb->fix_cr3=0;

	obuffer_init(&(pt_pcb->obuffer));
	pcb_table_info.num_task++;
	pcb_table_info.num_active++;
	
	if(ring<3){
		pt_pcb->cr3=RING0_CR3;
	}
	else{
		pt_pcb->cr3=ADDR_PROC_PGDIR(pid_empty);	
	}
}

void proc_init(void){
	loadbuf=kmalloc(0x100000);
}

//must be called within proc-body,because askhs...but this function is more safe,it's static
static void proc_load_seg(int pid,Elf32_Phdr*ph){
	if(ph->p_type!=1){
		oprintf("ph->p_type=%u,not a segment for loading..\n");
		return;
	}
	oprintf("got a segment for loading..\n");
	char*pt_read=loadbuf+ph->p_offset;
	char*pt_read_end=pt_read+ph->p_filesz-1;
	int curr_vpg=ph->p_vaddr>>12;
	//load into first ppg
	int ppg=alloc_page();
	int offset_in_pg=ph->p_vaddr&(0x1000-1);//取低12位，为页内偏移
	int pg_data_size=0x1000-offset_in_pg;

	memcp((char*)(ppg<<12)+offset_in_pg,pt_read,min(pg_data_size,pt_read_end-pt_read+1));
	proc_map_pg(pid,curr_vpg++,ppg,PG_USU,PG_RWW);
	pt_read+=pg_data_size;//至多移动这么多
	while(pt_read<=pt_read_end){
		ppg=alloc_page();//分配物理页
		memcp((char*)(ppg<<12),pt_read,min(0x1000,pt_read_end-pt_read+1));//将数据加载到物理页
		pt_read+=0x1000;//调整读指针
		proc_map_pg(pid,curr_vpg++,ppg,PG_USU,PG_RWW);//建立物理页和虚拟页的映射
	}
}

/**
 * 1,init a process's page-table to map stack and other room to it's virtual
 * space.
 * 2,create process-body from an elf-file
 */
static int proc_init_vspace(int pid,char*exec_file){
	loadfile(exec_file,loadbuf);	

	Elf32_Ehdr*header=(Elf32_Ehdr*)loadbuf;
	WORKON(header,Elf32_Ehdr);
	struinfo();

	Elf32_Phdr* ph=(Elf32_Phdr*)((u32)header+header->e_phoff);
	/**map kernel pages(0~16M) for normal proc*/
	proc_map_kpg(pid);
	/**map stack pages for normal proc*/
	proc_map_stackpg(pid);
	proc_map_pg(pid,ADDR_KERNEL_INFO/0x1000,ADDR_KERNEL_INFO/0x1000,PG_USU,PG_RWR);//how ugly!
	/**proc common pg-map done,including stack-pg and kernel-room-pg*/
	int ph_num=header->e_phnum;
	for(int ph_id=0;ph_id<ph_num;ph_id++){
		proc_load_seg(pid,ph);
		ph++;
	}
	oprintf("proc_init_vspace done,e_entry=%u\n",header->e_entry);
	return header->e_entry;
}

//must be called within proc-body,because askhs...but this function is more safe,it's static
void create_usr_process(char*exec_file,int prio,int time_slice,char*p_name,int father_pid){
	int pid_empty=getEmpty();
	int entry=proc_init_vspace(pid_empty,exec_file);
	proc_init_pcb(pid_empty,entry,prio,time_slice,p_name,3);
}

/**the following are so-called circular arr operations. */
void obuffer_init(OBUFFER* pt_obuffer){
	for(int i=0;i<size_buffer;i++) pt_obuffer->c[i]=0;
	pt_obuffer->head=0;
	pt_obuffer->tail=size_buffer-1;
	pt_obuffer->num=0;
}

void obuffer_push(OBUFFER* pt_obuffer,char c){
	assert(pt_obuffer->num<size_buffer)
	int next=(pt_obuffer->tail+1)%size_buffer;
	pt_obuffer->c[next]=c;
	
	pt_obuffer->num++;
	pt_obuffer->tail=next;
}

unsigned char obuffer_shift(OBUFFER* pt_obuffer){
	if(pt_obuffer->num==0) return 0;
	int head=pt_obuffer->head;
	int c=pt_obuffer->c[head];

	pt_obuffer->head=(head+1)%size_buffer;
	pt_obuffer->num--;
	return c;
}

