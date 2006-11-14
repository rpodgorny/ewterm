#include "x25_block.h"


struct packet {
	unsigned char family;
	unsigned char unk1;
	unsigned char dir;
	unsigned char pltype;
	unsigned short connid;
	unsigned char subseq;
	unsigned char unk2;
	unsigned short unk3;
	unsigned char tail;

	struct block *data;
};

void packet_delete(struct packet *);
struct packet *packet_deserialize(unsigned char *);
void packet_print(struct packet *);
int packet_serialize(struct packet *, unsigned char *);