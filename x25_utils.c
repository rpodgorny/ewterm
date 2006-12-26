#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <arpa/inet.h>

#include "x25_utils.h"

// TODO: currently hardcoded to 10 bytes of output
// returns number of digits
int to_bcd(unsigned char *buf, char *str) {
	int ret = 0;

	memset(buf, 0, 10);
	buf[0] = 0x36; // first byte must be 0x36, why?

	unsigned char tmpbuf[10];
	memset(tmpbuf, 0, 10);

	int i = 0, len = strlen(str);
	for (i = 0; i < len; i++) {
		if (!isdigit(str[i])) continue;

		unsigned char num = str[i] - '0';

		tmpbuf[ret/2] |= num << (4 * !(ret%2));

		ret++;
	}

	// reuse the var for encoded length
	len = ret/2;

	if (ret % 2) {
		// odd number of digits
		tmpbuf[ret/2] |= 0x0f;
		len++;
	}

	for (i = 0; i < len; i++) {
		buf[10-len+i] = tmpbuf[i];
	}

	return ret;
}

struct packet *login_packet(char *username, char *password) {
	struct packet *ret = packet_alloc();
	ret->family = 0xf1;
	ret->unk1 = 0xe0;
	ret->dir = 0x04;
	ret->pltype = 0x00;
	ret->connid = 0x9ec6;
	ret->subseq = 0;
	ret->unk2 = 0;
	ret->unk3 = 0x0675;//// same
	ret->tail = 0;

	ret->data = block_alloc();
	ret->data->id = 1;
	ret->data->data = NULL;

	unsigned char xxx[1024];

	xxx[0] = 0x01;
	block_addchild(ret->data, "1", xxx, 1);

	*(unsigned short *)(xxx) = htons(0);
	*(unsigned short *)(xxx+2) = htons(0); // job nr.
	*(unsigned short *)(xxx+4) = htons(0x675);
	*(unsigned short *)(xxx+6) = htons(0x4501);
	xxx[8] = 0x20;//
	xxx[9] = 0x0c;//
	block_addchild(ret->data, "2", xxx, 0x0a);

	block_addchild(ret->data, "4-1", (unsigned char *)"LINUX", 5);

	memset(xxx, 0xff, 0x01b8);
	block_addchild(ret->data, "4-3-1", xxx, 0x01b8);

	memset(xxx, 0x00, 2);
	block_addchild(ret->data, "4-3-2-1", xxx, 2);

	block_addchild(ret->data, "4-3-2-2", (unsigned char *)username, strlen(username));

	block_addchild(ret->data, "4-3-2-3", (unsigned char *)password, strlen(password));

	memset(xxx, 0x00, 1);
	block_addchild(ret->data, "4-3-2-5", xxx, 1);

	return ret;
}

struct packet *command_packet(char *c, int len) {
	struct packet *ret = packet_alloc();
	ret->family = 0xf1;
	ret->unk1 = 0xe0;
	ret->dir = 0x02;
	ret->pltype = 0x00;
	ret->connid = 0x9ecb;
	ret->subseq = 0;
	ret->unk2 = 0;
	ret->unk3 = 0x0675;
	ret->tail = 0;

	ret->data = block_alloc();
	ret->data->id = 4;
	ret->data->data = NULL;
	ret->data->nchildren = 0;

	unsigned char xxx[1024];

	xxx[0] = 0x05;
	block_addchild(ret->data, "1", xxx, 1);

	*(unsigned short *)(xxx) = htons(0);
	*(unsigned short *)(xxx+2) = htons(0); // job nr.
	*(unsigned short *)(xxx+4) = htons(0);
	*(unsigned short *)(xxx+6) = htons(0);
	xxx[8] = 0; // 0x20
	block_addchild(ret->data, "2", xxx, 0x09);

	block_addchild(ret->data, "6-1", (unsigned char *)c, len);

	return ret;
}

struct packet *command_confirmation_packet(unsigned short connid, unsigned short unk3, unsigned char tail, char *c, int len) {
	struct packet *ret = packet_alloc();
	ret->family = 0xf1;
	ret->unk1 = 0xe0;
	ret->dir = 0x03;
	ret->pltype = 0x01;
	ret->connid = connid;
	ret->subseq = 0;
	ret->unk2 = 0;
	ret->unk3 = unk3;
	ret->tail = tail;

	ret->data = block_alloc();
	ret->data->id = 8;
	ret->data->data = NULL;
	ret->data->nchildren = 0;

	unsigned char xxx[1024];

	xxx[0] = 0x05;
	block_addchild(ret->data, "1", xxx, 1);

	*(unsigned short *)(xxx) = htons(0);
	*(unsigned short *)(xxx+2) = htons(0);
	*(unsigned short *)(xxx+4) = htons(0);
	*(unsigned short *)(xxx+6) = htons(0);
	xxx[8] = 0;//0x20;
	xxx[9] = 0;//0x02;
	block_addchild(ret->data, "2", xxx, 0x0a);

	block_addchild(ret->data, "6-1", (unsigned char *)c, len);

	return ret;
}
