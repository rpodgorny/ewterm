#include <stdlib.h>
#include <unistd.h>
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

struct packet *login_packet(unsigned short sessid, char *username, char *password, char *newpassword, unsigned char reopen) {
	char pwd[256] = "";
	char npwd[256] = "";

	strcpy(pwd, password);
	if (newpassword) strcpy(npwd, newpassword);

	char *idx = index(pwd, ' ');
	if (idx) {
		// the user wants to change the password (separated by space)
		*idx = 0;
		strcpy(npwd, idx+1);
	}

	struct packet *ret = packet_alloc();
	ret->family = 0xf1;
	ret->unk1 = 0xe0;
	ret->dir = 0x04;
	ret->pltype = 0x00;
	ret->connid = 0;
	ret->subseq = 0;
	ret->unk2 = 0;
	ret->sessid = sessid;
	ret->tail = 0;

	ret->data = block_alloc();
	ret->data->id = 1;
	ret->data->data = NULL;
	ret->data->nchildren = 0;

	unsigned char xxx[1024];

	xxx[0] = 0x01;
	block_addchild(ret->data, "1", xxx, 1);

	*(unsigned short *)(xxx) = htons(0);
	*(unsigned short *)(xxx+2) = htons(0); // job nr.
	*(unsigned short *)(xxx+4) = htons(sessid);
	*(unsigned short *)(xxx+6) = htons(0);
	*(unsigned short *)(xxx+8) = htons(0);
	block_addchild(ret->data, "2", xxx, 0x0a);

	char hostname[256];
	gethostname(hostname, 256);
	block_addchild(ret->data, "4-1", (unsigned char *)hostname, strlen(hostname));

	memset(xxx, 0xff, 0x01b8);
	block_addchild(ret->data, "4-3-1", xxx, 0x01b8);

	memset(xxx, 0x00, 2);
	block_addchild(ret->data, "4-3-2-1", xxx, 2);

	block_addchild(ret->data, "4-3-2-2", (unsigned char *)username, strlen(username));

	block_addchild(ret->data, "4-3-2-3", (unsigned char *)pwd, strlen(pwd));
	if (strlen(npwd)) {
		block_addchild(ret->data, "4-3-2-4", (unsigned char *)npwd, strlen(npwd));
	}

	xxx[0] = reopen;
	block_addchild(ret->data, "4-3-2-5", xxx, 1);

	return ret;
}

struct packet *logout_packet(unsigned short sessid) {
	struct packet *ret = packet_alloc();
	ret->family = 0xf1;
	ret->unk1 = 0xe0;
	ret->dir = 0x0e;
	ret->pltype = 0x00;
	ret->connid = 0;
	ret->subseq = 0;
	ret->unk2 = 0;
	ret->sessid = sessid;
	ret->tail = 0;

	ret->data = block_alloc();
	ret->data->id = 0x0b;
	ret->data->data = NULL;
	ret->data->nchildren = 0;

	unsigned char xxx[1024];

	xxx[0] = 0x04;
	block_addchild(ret->data, "1", xxx, 1);

	*(unsigned short *)(xxx) = htons(0);
	*(unsigned short *)(xxx+2) = htons(0); // job nr.
	*(unsigned short *)(xxx+4) = htons(sessid);
	*(unsigned short *)(xxx+6) = htons(0);
	*(unsigned short *)(xxx+8) = htons(0);
	block_addchild(ret->data, "2", xxx, 0x0a);

	return ret;
}


struct packet *command_packet(unsigned short sessid, char *c, int len) {
	struct packet *ret = packet_alloc();
	ret->family = 0xf1;
	ret->unk1 = 0xe0;
	ret->dir = 0x02;
	ret->pltype = 0x00;
	ret->connid = 0;
	ret->subseq = 0;
	ret->unk2 = 0;
	ret->sessid = sessid;
	ret->tail = 0;

	ret->data = block_alloc();
	ret->data->id = 4;
	ret->data->data = NULL;
	ret->data->nchildren = 0;

	unsigned char xxx[1024];

	xxx[0] = 0x04;
	block_addchild(ret->data, "1", xxx, 1);

	*(unsigned short *)(xxx) = htons(0);
	*(unsigned short *)(xxx+2) = htons(0); // job nr.
	*(unsigned short *)(xxx+4) = htons(0);
	*(unsigned short *)(xxx+6) = htons(0);
	xxx[8] = 0;
	block_addchild(ret->data, "2", xxx, 0x09);

	block_addchild(ret->data, "6-1", (unsigned char *)c, len);

	return ret;
}

struct packet *command_confirmation_packet(unsigned short connid, unsigned short sessid, unsigned char tail, char *c, int len) {
	struct packet *ret = packet_alloc();
	ret->family = 0xf1;
	ret->unk1 = 0xe0;
	ret->dir = 0x03;
	ret->pltype = 0x01;
	ret->connid = connid;
	ret->subseq = 0;
	ret->unk2 = 0;
	ret->sessid = sessid;
	ret->tail = tail;

	ret->data = block_alloc();
	ret->data->id = 8;
	ret->data->data = NULL;
	ret->data->nchildren = 0;

	unsigned char xxx[1024];

	xxx[0] = 0x04;
	block_addchild(ret->data, "1", xxx, 1);

	*(unsigned short *)(xxx) = htons(0);
	*(unsigned short *)(xxx+2) = htons(0);
	*(unsigned short *)(xxx+4) = htons(0);
	*(unsigned short *)(xxx+6) = htons(0);
	*(unsigned short *)(xxx+8) = htons(0);
	block_addchild(ret->data, "2", xxx, 0x0a);

	block_addchild(ret->data, "6-1", (unsigned char *)c, len);

	return ret;
}
