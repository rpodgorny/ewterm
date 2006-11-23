#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <arpa/inet.h>
#include "x25_packet.h"


struct packet *packet_alloc() {
	struct packet *ret = malloc(sizeof(struct packet));

	memset(ret, 0, sizeof(struct packet));
/*
	ret->family = *buf;
	ret->unk1 = *(buf+1);
	ret->dir = *(buf+2);
	ret->pltype = *(buf+3);
	ret->connid = ntohs(*(unsigned short *)(buf+4));
	ret->subseq = *(buf+6);
	ret->unk2 = *(buf+7);
	ret->unk3 = ntohs(*(unsigned short *)(buf+8));
	ret->tail = *(buf+10);

	ret->data = block_deserialize(buf+11, NULL);
*/
	return ret;
}

int packet_serialize(struct packet *p, unsigned char *buf) {
	int ret = 11;

	*buf = p->family;
	*(buf+1) = p->unk1;
	*(buf+2) = p->dir;
	*(buf+3) = p->pltype;
	*(unsigned short *)(buf+4) = htons(p->connid);
	*(buf+6) = p->subseq;
	*(buf+7) = p->unk2;
	*(unsigned short *)(buf+8) = htons(p->unk3);
	*(buf+10) = p->tail;

	if (p->data) ret += block_serialize(p->data, buf+11);

	return ret;
}

struct packet *packet_deserialize(unsigned char *buf) {
	struct packet *ret = malloc(sizeof(struct packet));

	ret->family = *buf;
	ret->unk1 = *(buf+1);
	ret->dir = *(buf+2);
	ret->pltype = *(buf+3);
	ret->connid = ntohs(*(unsigned short *)(buf+4));
	ret->subseq = *(buf+6);
	ret->unk2 = *(buf+7);
	ret->unk3 = ntohs(*(unsigned short *)(buf+8));
	ret->tail = *(buf+10);

	ret->data = block_deserialize(buf+11, NULL);

	return ret;
}

void packet_print(struct packet *p) {
	printf("-------- PACKET PRINT BEGIN ---------\n");

	printf("fam: %#02x, unk1: %#02x, dir: %#02x, pltype: %#02x, connid: %#04x, subseq: %#02x, unk2: %#02x, unk3: %#04x, tail: %#02x\n", p->family, p->unk1, p->dir, p->pltype, p->connid, p->subseq, p->unk2, p->unk3, p->tail);

	if (p->data) block_print(p->data);

	printf("-------- PACKET PRINT END ---------\n\n");
}

void packet_delete(struct packet *p) {
	if (p->data) block_delete(p->data);
	free(p);
}
