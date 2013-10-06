/*
	All these operations are dependent on the reading and writing VGA registers
	For simplicity, we only use CRT Controller Registers
*/
#include<video_drv.h>
#include<utils.h>
#include<valType.h>

/**set cursor 'pod bytes offset to 0xb8000'*/
void set_cursor(unsigned pos){
	out_byte(CRTC_ADDR_REG,CURSOR_L);
	out_byte(CRTC_DATA_REG,pos&0xff);
	out_byte(CRTC_ADDR_REG,CURSOR_H);
	out_byte(CRTC_DATA_REG,pos>>8);
}

/**we map the video memory to screen from address 'pos'*/
void set_start(u32 pos){
	out_byte(CRTC_ADDR_REG,START_ADDR_L);
	out_byte(CRTC_DATA_REG,pos&0xff);
	out_byte(CRTC_ADDR_REG,START_ADDR_H);
	out_byte(CRTC_DATA_REG,pos>>8);
}

/**get the start address of video memory being mapped to screen*/
int get_start(void){
	int pos=0;
	out_byte(CRTC_ADDR_REG,START_ADDR_L);
	pos+=in_byte(CRTC_DATA_REG);
	out_byte(CRTC_ADDR_REG,START_ADDR_H);
	pos+=in_byte(CRTC_DATA_REG)<<8;
	return pos;
}
