#include "x25_block.h"


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

void packet_delete(struct packet *);
void packet_print(struct packet *);
int packet_serialize(struct packet *, unsigned char *);

void preamble_delete(struct preamble *);
struct preamble *preamble_deserialize(char *);
void preamble_print(struct preamble *);
void preamble_serialize(struct preamble *, char *);
