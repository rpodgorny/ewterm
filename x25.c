#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <arpa/inet.h>
#include "x25.h"

struct block *block_deserialize(char *data, struct block *parent) {
	printf("block_deserialize\n");

	struct block *ret = malloc(sizeof(struct block));

	ret->id = *(unsigned char *)data;
	ret->len = ntohs(*(unsigned short *)(data+1));

	ret->data = malloc(ret->len);
	memcpy(ret->data, data+3, ret->len);

	ret->nchildren = 0;
/*	char *ptr = data+3;
	while (ptr < data+3+ret->len) {
		ret->children[ret->nchildren++] = block_deserialize(ptr, ret);
	}
	if (ret->nchildren > 0) {
		free(ret->data);
		ret->data = NULL;
	}
*/
	ret->parent = parent;

	return ret;
}

void block_delete(struct block *b) {
	if (!b) return;

	int i = 0;
	for (i = 0; i < b->nchildren; i++) block_delete(b->children[i]);

	if (b->data) free(b->data);
	free(b);
}

void getpath(struct block *b, char *path) {
	int ipath[1000];
	int pathlen = 0;

	struct block *ptr = b;
	while (ptr) {
		ipath[pathlen++] = ptr->id;
		ptr = ptr->parent;
	}

	strcpy(path, "");
	int i = 0;
	for (i = pathlen; i > 0; i--) {
		if (i == pathlen) {
			sprintf(path+strlen(path), "%d", ipath[i]);
		} else {
			sprintf(path+strlen(path), "-%d", ipath[i]);
		}
	}
}

struct block *getchild(struct block *parent, char *path) {
	int ipath[1000];
	int pathlen = 0;

	ipath[pathlen++] = atoi(path);

	char *ptr = path;
	while ((ptr = index(ptr, '-')) != NULL) {
		ptr++;
		ipath[pathlen++] = atoi(ptr);
	}

	// Go find it
	struct block *ret = parent;
	int depth = 0;
	while (depth < pathlen) {
		int i = 0;
		for (i = 0; i < ret->nchildren; i++) {
			struct block *child = ret->children[i];
			if (child->id == ipath[depth]) {
				ret = ret->children[i];
			}
		}

		// not found
		if (i == ret->nchildren) {
			ret = NULL;
			break;
		}
	}

	return ret;
}

char *packet_serialize(struct packet *p) {
	int pre_size = preamble_size(p->pre);
	int b_size = block_size(p->data);

	char *ret = malloc(pre_size + b_size);

	preamble_serialize(p->pre, ret);
	block_serialize(p->data, ret+pre_size);

	return ret;
}

int preamble_size(struct preamble *p) {
	return 11;
}

int block_size(struct block *b) {
	int ret = 0;

	if (b->data) {
		ret += b->len;
	} else {
		int i = 0;
		for (i = 0; i < b->nchildren; i++) ret += block_size(b->children[i]);
	}

	return ret + 3;
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

void block_serialize(struct block *b, char *buf) {
	*buf = b->id;

	if (b->data) {
		*(buf+1) = htons(b->len);
		memcpy(buf+3, b->data, b->len);
	} else {
		unsigned short len = 0;

		int i = 0;
		for (i = 0; i < b->nchildren; i++) {
			block_serialize(b->children[i], buf+3+len);
			len += block_size(b->children[i]);
		}

		*(buf+1) = htons(len);
	}
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

void block_print(struct block *b) {
	printf("id: %d, len: %d\n", b->id, b->len);
	
	char *ptr = b->data;
	while (ptr < b->data+3+b->len) {
		printf("0x%2x ", *ptr);
		ptr++;
	}
}
