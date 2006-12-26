#ifndef EW__X25_BLOCK_H
#define EW__X25_BLOCK_H

struct block {
	int id;
	int len;
	unsigned char *data;

	struct block *children[1000];
	int nchildren;

	struct block *parent;
};

struct block *block_alloc();
void block_addchild(struct block *, char *, unsigned char *, int);
void block_delete(struct block *);
struct block *block_deserialize(unsigned char *, int, struct block *);
struct block *block_getchild(struct block *, char *);
void block_getpath(struct block *, char *);
void block_print(struct block *);
int block_serialize(struct block *, unsigned char *);
int block_haschildren(unsigned char *, int);

#endif
