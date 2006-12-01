#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

#include "x25_utils.h"

struct packet *login_packet(char *username, char *password) {
	struct packet *ret = malloc(sizeof(struct packet));
	ret->family = 0xf1;
	ret->unk1 = 0xe0;
	ret->dir = 0x04;
	ret->pltype = 0x00;
	ret->connid = 0x9ec6;
	ret->subseq = 0;
	ret->unk2 = 0;
	ret->unk3 = 0x0675;//// same
	ret->tail = 0;

	ret->data = malloc(sizeof(struct block));
	ret->data->id = 1;
	ret->data->data = NULL;

	unsigned char xxx[1024];

	xxx[0] = 0x01;
	block_addchild(ret->data, "1", xxx, 1);

	*(unsigned short *)(xxx) = htons(0);
	*(unsigned short *)(xxx+2) = htons(0); // job nr.
	*(unsigned short *)(xxx+4) = htons(0);
	xxx[6] = 0x45;//
	xxx[7] = 0x01;//
	xxx[8] = 0x20;//
	block_addchild(ret->data, "2", xxx, 0x09);

	block_addchild(ret->data, "4-1", (unsigned char *)"ENM", 3);

	memset(xxx, 0xff, 0x01b8);
	block_addchild(ret->data, "4-3-1", xxx, 0x01b8);

	memset(xxx, 0x00, 2);
	block_addchild(ret->data, "4-3-2-1", xxx, 2);

	block_addchild(ret->data, "4-3-2-2", username, strlen(username));

	unsigned char pass[] = {0x95, 0x02, 0x6e, 0x55, 0x12, 0x55, 0x97, 0xe1, 0x33, 0x6e, 0x43, 0xa7, 0xb3, 0x53, 0x07, 0x75, 0x41, 0x7b, 0x0c, 0x06, 0x3a, 0xa8, 0x7d, 0xd5, 0x09, 0x00};

	block_addchild(ret->data, "4-3-2-3", pass, sizeof(pass));

	memset(xxx, 0x00, 1);
	block_addchild(ret->data, "4-3-2-5", xxx, 1);

	return ret;
}

struct packet *command_packet(char *c, int len) {
	struct packet *ret = malloc(sizeof(struct packet));
	ret->family = 0xf1;
	ret->unk1 = 0xe0;
	ret->dir = 0x02;
	ret->pltype = 0x00;
	ret->connid = 0x9ecb;
	ret->subseq = 0;
	ret->unk2 = 0;
	ret->unk3 = 0x0675;
	ret->tail = 0;

	ret->data = malloc(sizeof(struct block));
	ret->data->id = 4;
	ret->data->data = NULL;
	ret->data->nchildren = 0;

	unsigned char xxx[1024];

	xxx[0] = 0x05;
	block_addchild(ret->data, "1", xxx, 1);

	*(unsigned short *)(xxx) = htons(0);
	*(unsigned short *)(xxx+2) = htons(0); // job nr.
	xxx[4] = 0xdd;
	xxx[5] = 0xe1;
	xxx[6] = 0x03;
	xxx[7] = 0x01;
	xxx[8] = 0x20;
	block_addchild(ret->data, "2", xxx, 0x09);

	block_addchild(ret->data, "6-1", (unsigned char *)c, len);

	return ret;
}

struct packet *command_confirmation_packet(unsigned short connid, unsigned short unk3, unsigned char tail, char *c, int len) {
	struct packet *ret = malloc(sizeof(struct packet));
	ret->family = 0xf1;
	ret->unk1 = 0xe0;
	ret->dir = 0x03;
	ret->pltype = 0x01;
	ret->connid = connid;
	ret->subseq = 0;
	ret->unk2 = 0;
	ret->unk3 = unk3;
	ret->tail = tail;

	ret->data = malloc(sizeof(struct block));
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
