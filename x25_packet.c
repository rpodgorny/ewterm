#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <arpa/inet.h>
#include "x25_packet.h"


int packet_serialize(struct packet *p, unsigned char *buf) {
	int pre_size = preamble_size(p->pre);
	int b_size = block_size(p->data);

	preamble_serialize(p->pre, buf);
	block_serialize(p->data, buf+pre_size);

	return pre_size+b_size;
}

int preamble_size(struct preamble *p) {
	return 11;
}

struct packet *packet_deserialize(char *buf) {
	struct packet *ret = malloc(sizeof(struct packet));

	ret->pre = preamble_deserialize(buf);
	ret->data = block_deserialize(buf+11, NULL);

	return ret;
}

void packet_print(struct packet *p) {
	preamble_print(p->pre);
	block_print(p->data);
}

void preamble_delete(struct preamble *p) {
	if (!p) return;

	free(p);
}

void packet_delete(struct packet *p) {
	if (!p) return;

	preamble_delete(p->pre);
	block_delete(p->data);
	free(p);
}

void preamble_print(struct preamble *p) {
	printf("fam: %x, unk1: %x, dir: %x, pltype: %x, connid: %x, subseq: %x, unk2: %x, unk3: %x, tail: %x\n", p->family, p->unk1, p->dir, p->pltype, p->connid, p->subseq, p->unk2, p->unk3, p->tail);
}


struct preamble *preamble_deserialize(char *buf) {
	struct preamble *ret = malloc(sizeof(struct preamble));

	ret->family = *buf;
	ret->unk1 = *(buf+1);
	ret->dir = *(buf+2);
	ret->pltype = *(buf+3);
	ret->connid = ntohs(*(unsigned short *)(buf+4));
	ret->subseq = *(buf+6);
	ret->unk2 = *(buf+7);
	ret->unk3 = ntohs(*(unsigned short *)(buf+8));
	ret->tail = *(buf+10);

	return ret;
}

void preamble_serialize(struct preamble *p, char *buf) {
	*buf = p->family;
	*(buf+1) = p->unk1;
	*(buf+2) = p->dir;
	*(buf+3) = p->pltype;
	*(unsigned short *)(buf+4) = htons(p->connid);
	*(buf+6) = p->subseq;
	*(buf+7) = p->unk2;
	*(unsigned short *)(buf+8) = htons(p->unk3);
	*(buf+10) = p->tail;
}
