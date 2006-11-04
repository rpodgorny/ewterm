#include <stdlib.h>
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
};

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
		for (i = 0; i < (*ret).nchildren; i++) {
			if ((*((*ret).children[i])).id == ipath[depth]) {
				ret = (*ret).children[i];
			}
		}

		// not found
		if (i == (*ret).nchildren) {
			ret = NULL;
			break;
		}
	}

	return ret;
}
