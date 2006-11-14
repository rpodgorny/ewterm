struct block {
	int id;
	int len;
	char *data;

	struct block *children[1000];
	int nchildren;

	struct block *parent;
};

void block_delete(struct block *);
struct block *block_deserialize(char *, struct block *);
struct block *block_getchild(struct block *, char *);
void block_getpath(struct block *, char *);
void block_print(struct block *);
void block_serialize(struct block *, char *);
int haschildren(char *, int);
