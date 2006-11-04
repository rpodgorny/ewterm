#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

struct preamble {
	unsigned char family;
	unsigned char unk1;
	unsigned char dir;
	unsigned char pltype;
	unsigned short connid;
	unsigned char subseq;
	unsigned char unk2;
	unsigned short unk3;
	unsigned char tail;
};

struct pack {
	struct preamble *pre;
	struct block *data;
};

struct block {
	int id;
	int len;
	char *data;

	struct block *children[1000];
	int nchildren;

	struct block *parent;
};

struct block *toblock(struct block *parent, char *data) {
	struct block *ret = malloc(sizeof(struct block));

	ret->id = *(unsigned char *)data;
	ret->len = *(unsigned short *)(data+1);
	ret->data = data+3;

	ret->nchildren = 0;
	char *ptr = data+3;
	while (ptr < data+3+ret->len) {
		ret->children[ret->nchildren++] = toblock(ret, ptr);
	}

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
		sprintf(path+strlen(path), "-%d", ipath[i]);
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
