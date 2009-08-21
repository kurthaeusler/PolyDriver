#include <string.h>
#include <stdio.h>
#include "lzn.h"
#include "atob.h"

unsigned char lzn[16];
int devno;
unsigned char *GenerateLZN(unsigned char *name, int devno)	// name = 'PTX' or 'GLX' etc., devno = serial Nummer
{
	int y[5];
	if ((strlen((const char *) name) != 3) || (devno == 0)
	    || (devno > 0x00ffffff)) {
		return "";
	}
	y[0] = (A2I['P'] << 10) + (A2I['T'] << 5) + A2I['X'];	// 'PTX'
	y[1] = (A2I[name[0]] << 10) + (A2I[name[1]] << 5) + A2I[name[2]];	//  ie. 'GLX'

	// use static key (0xCA5936)

	// use variable key 
//      y[2] = y[2] ^ m_vkeyh;
//      y[3] = y[3] ^ m_vkeyl;
	lzn[0] = name[0];
	lzn[1] = name[1];
	lzn[2] = name[2];
	y[2] = (devno >> 15) ^ 0x3149;	// high part of unit number
	lzn[3] = I2A[((y[2] >> 10) & 0x1F)];
	lzn[4] = I2A[((y[2] >> 5) & 0x1F)];
	lzn[5] = I2A[(y[2] & 0x1F)];
	y[3] = (devno & 0x7FFF) ^ 0x2466;	// low  part of unit number  
	lzn[6] = I2A[((y[3] >> 10) & 0x1F)];
	lzn[7] = I2A[((y[3] >> 5) & 0x1F)];
	lzn[8] = I2A[(y[3] & 0x1F)];
	y[4] = 0x10000 - (y[0] + y[1] + y[2] + y[3]);
	lzn[9] = I2A[((y[4] >> 10) & 0x1F)];
	lzn[10] = I2A[((y[4] >> 5) & 0x1F)];
	lzn[11] = I2A[(y[4] & 0x1F)];
	lzn[12] = '\0';
	return (unsigned char *) lzn;
}

int EncodeLZN(unsigned char *lzn)
{
	int y[5];
	if (strlen((const char *) lzn) != 12) {
		return 0;
	}
	y[0] = (A2I['P'] << 10) + (A2I['T'] << 5) + A2I['X'];	// 'PTX'
	y[1] = (A2I[lzn[0]] << 10) + (A2I[lzn[1]] << 5) + A2I[lzn[2]];	// i.e. 'GLX'
	y[2] = (A2I[lzn[3]] << 10) + (A2I[lzn[4]] << 5) + A2I[lzn[5]];	// high part of unit number
	y[3] = (A2I[lzn[6]] << 10) + (A2I[lzn[7]] << 5) + A2I[lzn[8]];	// low  part of unit number  
	y[4] = (A2I[lzn[9]] << 10) + (A2I[lzn[10]] << 5) + A2I[lzn[11]];	// check sum
	if (((y[0] + y[1] + y[2] + y[3] + y[4]) != 0x10000)
	    && ((y[0] + y[1] + y[2] + y[3] + y[4]) != 0x18000)) {
		return 0;
	}
	// use variable key 
//      y[2] = y[2] ^ m_vkeyh;
//      y[3] = y[3] ^ m_vkeyl;

	// use static key (0xCA5936)
	y[2] = y[2] ^ 0x3149;	// high part of unit number
	y[3] = y[3] ^ 0x2466;	// low  part of unit number  
	devno = (y[2] << 15) + (y[3] & 0x7fff);
	return devno;
}
