#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "x25.h"

struct block *raw_to_block(char *data, struct block *parent) {
	struct block *ret = malloc(sizeof(struct block));

	ret->id = *(unsigned char *)data;
	ret->len = *(unsigned short *)(data+1);
	ret->data = data+3;

	ret->nchildren = 0;
	char *ptr = data+3;
	while (ptr < data+3+ret->len) {
		ret->children[ret->nchildren++] = raw_to_block(ptr, ret);
	}
	if (ret->nchildren > 0) ret->data = NULL;

	ret->parent = parent;

	return ret;
}

void deleteblock(struct block *b) {
	int i = 0;
	for (i = 0; i < b->nchildren; i++) deleteblock(b->children[i]);

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
	int size = preamble_size(p->pre);
	size += block_size(p->data);

	char *ret = malloc(size);

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
