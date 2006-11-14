#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <arpa/inet.h>
#include "x25_block.h"


struct block *block_deserialize(unsigned char *buf, struct block *parent) {
	struct block *ret = malloc(sizeof(struct block));

	ret->id = *buf;
	ret->data = NULL;
	ret->len = 0;
	ret->nchildren = 0;

	if (haschildren(buf+3, ret->len)) {
		unsigned char *ptr = buf+3;
		while (ptr < buf+3+ret->len) {
			ret->children[ret->nchildren] = block_deserialize(ptr, ret);
			ptr += ret->children[ret->nchildren]->len + 3;
			ret->nchildren++;
		}
	} else {
		ret->data = malloc(ret->len);
		memcpy(ret->data, buf+3, ret->len);
		ret->len = ntohs(*(unsigned short *)(buf+1));
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

int haschildren(unsigned char *buf, int len) {
	int pos = 0;

	while (pos < len) {
		int l = ntohs(*(unsigned short *)(buf+1+pos));
		if (l == 0) break;
		pos += l+3;
	}

	if (pos == len) return 1;

	return 0;
}

void block_addchild(struct block *root, char *path, unsigned char *buf, int len) {
	int ipath[1000];
	int pathlen = 0;

	ipath[pathlen++] = atoi(path);

	char *ptr = path;
	while ((ptr = index(ptr, '-')) != NULL) {
		ptr++;
		if (*ptr == 'x') {
			ipath[pathlen++] = -1;
		} else {
			ipath[pathlen++] = atoi(ptr);
		}
	}

	// Go find it, create on the run
	struct block *cur = root;
	int depth = 0;
	for (depth = 0; depth < pathlen; depth++) {
		int i = 0;
		for (i = 0; i < cur->nchildren; i++) {
			if (ipath[depth] == -1 || cur->children[i]->id == ipath[depth]) {
				cur = cur->children[i];
				break;
			}
		}

		// not found, create it
		if (i == cur->nchildren) {
			cur->children[i] = malloc(sizeof(struct block));
			cur->children[i]->id = ipath[depth];
			cur->children[i]->nchildren = 0;

			cur->children[i]->data = NULL;
			cur->children[i]->len = 0;

			cur->children[i]->parent = cur;

			cur->nchildren++;

			cur = cur->children[i];
		}
	}

	cur->data = malloc(len);
	memcpy(cur->data, buf, len);
	cur->len = len;
}

struct block *block_getchild(struct block *parent, char *path) {
	int ipath[1000];
	int pathlen = 0;

	ipath[pathlen++] = atoi(path);

	char *ptr = path;
	while ((ptr = index(ptr, '-')) != NULL) {
		ptr++;
		if (*ptr == 'x') {
			ipath[pathlen++] = -1;
		} else {
			ipath[pathlen++] = atoi(ptr);
		}
	}

	// Go find it
	struct block *ret = parent;
	int depth = 0;
	for (depth = 0; depth < pathlen; depth++) {
		int i = 0;
		for (i = 0; i < ret->nchildren; i++) {
			if (ipath[depth] == -1 || ret->children[i]->id == ipath[depth]) {
				ret = ret->children[i];
				break;
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

int block_serialize(struct block *b, unsigned char *buf) {
	int ret = 0;

	*buf = b->id;

	if (b->data) {
		*(unsigned short *)(buf+1) = htons(b->len);
		memcpy(buf+3, b->data, b->len);
		ret = b->len + 3;
	} else {
		unsigned short len = 0;

		int i = 0;
		for (i = 0; i < b->nchildren; i++) {
			len += block_serialize(b->children[i], buf+3+len);
		}

		*(unsigned short *)(buf+1) = htons(len);

		ret = len + 3;
	}

	return ret;
}

void block_print(struct block *b) {
	char path[1000];
	block_getpath(b, path);
	printf("path: %s, len (direct): %d\n", path, b->len);

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
			if (*(char *)ptr >= 0x20) printf("%c", *(char *)ptr);
			ptr++;
		}
		printf("\n");
	} else {
		int i = 0;
		for (i = 0; i < b->nchildren; i++) {
			block_print(b->children[i]);
		}
	}
}
