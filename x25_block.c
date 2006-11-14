#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <arpa/inet.h>
#include "x25_block.h"

struct block *block_deserialize(char *data, struct block *parent) {
	struct block *ret = malloc(sizeof(struct block));

	ret->id = *(unsigned char *)data;
	ret->len = ntohs(*(unsigned short *)(data+1));
	ret->data = NULL;
	ret->nchildren = 0;

	if (haschildren(data+3, ret->len)) {
		char *ptr = data+3;
		while (ptr < data+3+ret->len) {
			ret->children[ret->nchildren] = block_deserialize(ptr, ret);
			ptr += ret->children[ret->nchildren]->len + 3;
			ret->nchildren++;
		}
	} else {
		ret->data = malloc(ret->len);
		memcpy(ret->data, data+3, ret->len);
	}

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

void block_getpath(struct block *b, char *path) {
	int ipath[1000];
	int pathlen = 0;

	struct block *ptr = b;
	while (ptr) {
		ipath[pathlen++] = ptr->id;
		ptr = ptr->parent;
	}

	strcpy(path, "");
	int i = 0;
	for (i = pathlen-1; i >= 0; i--) {
		if (i == pathlen-1) {
			sprintf(path+strlen(path), "%d", ipath[i]);
		} else {
			sprintf(path+strlen(path), "-%d", ipath[i]);
		}
	}
}

int haschildren(char *buf, int len) {
	int pos = 0;

	while (pos < len) {
		int l = ntohs(*(unsigned short *)(buf+1+pos));
		if (l == 0) break;
		pos += l+3;
	}

	if (pos == len) return 1;

	return 0;
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
void block_print(struct block *b) {
	char path[1000];
	block_getpath(b, path);
	printf("path: %s, len: %d\n", path, b->len);

	if (b->data) {
		printf("data: ");
		unsigned char *ptr = b->data;
		while (ptr < b->data + b->len) {
			printf("%02x ", *ptr);
			ptr++;
		}
		printf("\n");

		printf("datac: ");
		ptr = b->data;
		while (ptr < b->data + b->len) {
			printf("%c", *(char *)ptr);
			ptr++;
		}
		printf("\n");
	} else {
		int i = 0;
		for (i = 0; i < b->nchildren; i++) block_print(b->children[i]);
	}
}
