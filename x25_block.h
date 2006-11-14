struct block {
	int id;
	int len;
	unsigned char *data;

	struct block *children[1000];
	int nchildren;

	struct block *parent;
};

void block_addchild(struct block *, char *, unsigned char *, int);
void block_delete(struct block *);
struct block *block_deserialize(unsigned char *, struct block *);
struct block *block_getchild(struct block *, char *);
void block_getpath(struct block *, char *);
void block_print(struct block *);
int block_serialize(struct block *, unsigned char *);
int haschildren(unsigned char *, int);
