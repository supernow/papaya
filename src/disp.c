/*To provide basic print function for kernel*/
#include<ku_utils.h>
#include<video_drv.h>
#include<disp.h>
#include<utils.h>
#include<valType.h>
#define buffer_len 12
#define EDI_ENTER edi=(((edi/160)+1)*160);
/* number of bytes in one page ,a char variable shuould be represented by two bytes */
#define PAGE_SIZE (80*25*2)
/*byte offset in the end*/
#define BOUND (PAGE_SIZE*2-2)
/**i don't want to to translate this part
1,k_show_chars函数的主体实现，这个代码块没有通用功能，只是让k_show_chars函数更好看一些
2,目前是唯一的写屏接口，要具备upsend_page,滚屏，光标跟随，转义符识别功能
3,先判别当前指向是否为控制字符，若不是，则简单输出到屏幕即可
4,while每写入一个字符后，或回车,退格后，都要检查edi是否越界，并调整之
5,普通字符会让edi增2,控制字符就复杂了。
6,写屏函数要做两样事：按需要写显存；调整edi。至于边界检查，滚屏，光标跟随都是完善性的函数。
*/
#define LOOP(exp)\
while(exp){\
	if(*pt_read=='\n'){\
		EDI_ENTER\
	}\
	else if(*pt_read=='\b'){\
		if(edi>=start_line*160+2){\
			edi-=2;\
			*(pt_video+edi)=0;\
		}\
	}\
	else{\
		*(pt_video+edi)=*pt_read;\
		edi+=2;\
	}\
	pt_read++;\
	k_checkbound();	\
}

//	*(pt_video+edi+1)=0x4;
static char* pt_video=(char*)0xb8000;
static char asciis_buffer[buffer_len];
/*to save ascii code of every digit of a number */
static int start_line=0;
/*at which line to start showing */
static int width=-1;
/*must be initialized below zero.support printf format like "%4s"*/

void oprintf(char*format,...){
	char*pt_curr=format;
	unsigned arg_id=0;
	int*pt_arg0=(int*)&format;/*pa_arg0 points to the first argument of current stack*/
	while(*pt_curr!=0){
		if(*pt_curr=='%'){
			arg_id++;
			pt_curr++;

			/**here we support width-fixed
			 * usr may use variable as width   eg:printf("%*s",width);
			 */
			if(*pt_curr=='*'){
				width=*(pt_arg0+arg_id);/**if we meet a '*' after '%',we should fetch a corresponding argument from stack*/
				arg_id++;
				pt_curr++;
				goto just_show_var;
			}
			
			/**here we  support ... eg:printf("%10s","a string");*/
			eat_dec(pt_curr,width);
		just_show_var:
			k_show_var(*(pt_arg0+arg_id),*pt_curr);
			/*Notice:pt_arg0 is the last one pushed in stack,as a result
			argument address is pt_arg0+arg_id.*/
			pt_curr++;
		}
		else{
			k_show_chars(pt_curr,1);/**we finished handling a flag-segment*/
			while((*pt_curr!='%')&&(*pt_curr!=0)) pt_curr++;	/**jump to the next flag*/
		}
		/* Now pt_curr points at the head of a new flag-segment or string-tail */
	}
}

void k_show_var(unsigned x,int val_type){
	switch(val_type){
		case 's':/*val_type=string,just call k_show_chars */
			k_show_chars((char*)x,0);
			break;
		default: /*case 'c','u','d','x',val_type=digit,use asciis_buffer to convert*/
			write_asciis_buffer(x,val_type);
			show_asciis_buffer();
			break;
	}
}

/**scroll up if text fall out of last line on screen(line 79)*/
void k_scroll(void){
	int new_start_line=(edi/160)-25+1;
	if(new_start_line>start_line){
		set_start(new_start_line*80);
		start_line=new_start_line;
	}

}

/**relocate 'edi' to valid video memory range if beyond*/
void k_checkbound(void){
	if(edi>BOUND){
		memcp(pt_video,pt_video+PAGE_SIZE,PAGE_SIZE);/* copy page-1 to page-0 */
   		set_start(0);/* display form the page-0   */
		memsetw((u16*)(pt_video+PAGE_SIZE),PAGE_SIZE/2,0x200);
		edi-=PAGE_SIZE;/*Adjust the write pointer */
		start_line=0;
	}
}

/* if end_flag!=0,k_show_chars may crack for width-support
   This function can support Style such as printf("%*s")
*/
void k_show_chars(char*pt_head,u32 end_flag){
	char*pt_read=pt_head;

	int chars_len=strlen(pt_head);
	int padden=width-chars_len;
	switch(end_flag){
		case 0:
			LOOP(((width--!=0)&&*pt_read!=0))					
			break;
		case 1:
			LOOP((width--!=0)&&(*pt_read!=0)&&(*pt_read!='%'))					
			break;
		default:
			assert(0);
	}
	if(padden>0) edi+=padden*2;
	
 	k_scroll();
	/*cursor following*/
	set_cursor(edi/2);
	width=-1;
}

void init_asciis_buffer(void){
	for(int i=0;i<buffer_len;i++){
		asciis_buffer[i]=0;
	}
}

void show_asciis_buffer(void){
	int i=0;
	while(asciis_buffer[i]==0&&i<buffer_len-1) i++;
	k_show_chars(asciis_buffer+i,0);
}

/**
 * break a digit into single letter sequence based on format
 * eg:160 will be broken into '1','6','0' in decimal,and into '0' 'X' '2' '0'
 * in hexadecimal
 */
void write_asciis_buffer(unsigned x,unsigned val_type){
	init_asciis_buffer();
	unsigned offset=buffer_len-1-1;
	unsigned temp=x;
	switch(val_type){
		case 'c':
			asciis_buffer[offset]=x;
			break;

		/*	Decomposition unsigned integer */
		case 'u':
			while(temp>9){
				asciis_buffer[offset]=temp%10+48;
				temp/=10;
				offset--;
			}
			asciis_buffer[offset]=temp+48;
			break;
		case 'x':
			while(temp>0xf){
				unsigned i=temp%16;
				asciis_buffer[offset]=i<=9?i+48:i+87;
				temp/=16;
				offset--;
			}
			/*Write 0X prefix for the lastascii */
			asciis_buffer[offset]=temp<=9?temp+48:temp+87;
			asciis_buffer[offset-1]='X';
			asciis_buffer[offset-2]='0';
			break;
		default:
			assert(0)
			break;
	}
}
/** clear video memory and reset related CRT registers*/
void k_screen_reset(void){
	memsetw((u16*)pt_video,PAGE_SIZE/2*2,0x700);
	set_start(0);
	set_cursor(0);
	edi=0;
}














