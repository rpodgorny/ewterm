#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <arpa/inet.h>

#include "x25_packet.h"


struct packet *packet_alloc() {
	struct packet *ret = malloc(sizeof(struct packet));
	memset(ret, 0, sizeof(struct packet));
	return ret;
}

int packet_serialize(struct packet *p, unsigned char *buf) {
	if (!p) return 0;

	int ret = 11;

	*buf = p->family;
	*(buf+1) = p->unk1;
	*(buf+2) = p->dir;
	*(buf+3) = p->pltype;
	*(unsigned short *)(buf+4) = htons(p->connid);
	*(buf+6) = p->subseq;
	*(buf+7) = p->unk2;
	*(unsigned short *)(buf+8) = htons(p->sessid);
	*(buf+10) = p->tail;

	if (p->data) ret += block_serialize(p->data, buf+11);

	return ret;
}

struct packet *packet_deserialize(unsigned char *buf, int len) {
	if (len < 11) return NULL;

	struct packet *ret = malloc(sizeof(struct packet));

	ret->family = *buf;
	ret->unk1 = *(buf+1);
	ret->dir = *(buf+2);
	ret->pltype = *(buf+3);
	ret->connid = ntohs(*(unsigned short *)(buf+4));
	ret->subseq = *(buf+6);
	ret->unk2 = *(buf+7);
	ret->sessid = ntohs(*(unsigned short *)(buf+8));
	ret->tail = *(buf+10);

	ret->data = NULL;
	ret->rawdata = NULL;
	ret->rawdatalen = 0;

	if (len > 11) {
		// try to deserialize inner block
		ret->data = block_deserialize(buf+11, len-11, NULL);

		if (!ret->data) {
			// failed, save as raw data
			ret->rawdata = malloc(len-11);
			memcpy(ret->rawdata, buf+11, len-11);
			ret->rawdatalen = len-11;
		}
	}

	return ret;
}

void packet_print(struct packet *p) {
	printf("-------- PACKET PRINT BEGIN ---------\n");

	printf("fam: %#02x, unk1: %#02x, dir: %#02x, pltype: %#02x, connid: %#04x, subseq: %#02x, unk2: %#02x, sessid: %#04x, tail: %#02x\n", p->family, p->unk1, p->dir, p->pltype, p->connid, p->subseq, p->unk2, p->sessid, p->tail);

	if (p->data) block_print(p->data);

	printf("-------- PACKET PRINT END ---------\n\n");
}

void packet_delete(struct packet *p) {
	if (!p) return;

	if (p->data) block_delete(p->data);
	if (p->rawdata) free(p->rawdata);

	free(p);
}
