
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

struct packet {
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

struct block *block_deserialize(char *, struct block *);
struct preamble *preamble_deserialize(char *);
void block_delete(struct block *);
void getpath(struct block *, char *);
struct block *getchild(struct block *, char *);
void block_serialize(struct block *, char *);
void preamble_serialize(struct preamble *, char *);
void packet_print(struct packet *);
void packet_delete(struct packet *);
void preamble_delete(struct preamble *);
