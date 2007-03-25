#ifndef EW__X25_PACKET_H
#define EW__X25_PACKET_H

#include "x25_block.h"


struct packet {
	unsigned char family;
	unsigned char unk1;
	unsigned char dir;
	unsigned char pltype;
	unsigned short connid; // connection id (for one job)
	unsigned char subseq;
	unsigned char unk2;
	unsigned short sessid; // session id
	unsigned char tail;

	struct block *data;

	unsigned char *rawdata;
	int rawdatalen;
};

struct packet *packet_alloc();
void packet_delete(struct packet *);
struct packet *packet_deserialize(unsigned char *, int);
void packet_print(struct packet *);
int packet_serialize(struct packet *, unsigned char *);

#endif
