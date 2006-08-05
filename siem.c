/* This is communication between ewrecv and x25 exchange. */

struct Preamble {
	unsigned char family;
	unsigned char unknown1;
	unsigned char direction;
	unsigned char payload;
	unsigned short conn_id;
	unsigned char subseq;
	unsigned char unknown2;
	unsigned short unknown3;
	unsigned char tail;
}

struct Block {
	unsigned char id;
	unsigned short len;
	void *data;
}

Block *ToBlock(int i, void *data) {
	
}
