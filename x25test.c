#include <stdio.h>
#include <sys/socket.h>
#include <linux/sockios.h>
#include <linux/x25.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include "x25_packet.h"


struct packet *line_to_packet() {
	FILE *f = fopen("../tdump2", "r");

	char line[100000];
	unsigned char buf[100000];

	fgets(line, 100000, f);

	int pos = 0;
	char *p = line;
	while (p) {
		//buf[pos] = atoi(p+1, 16);
		buf[pos] = strtol(p, (char **)NULL, 16);
//printf("%x ", buf[pos]);
		pos++;
		p = index(p+1, ' ');
	}
//printf("\n");

	fclose(f);

	return packet_deserialize(buf);
}

struct packet *command_packet() {
	struct packet *ret = malloc(sizeof(struct packet));
	ret->family = 0xf1;
	ret->unk1 = 0xe0;
	ret->dir = 0x02;
	ret->pltype = 0x00;
	ret->connid = 0xe15b;
	ret->subseq = 0;
	ret->unk2 = 0;
	ret->unk3 = 0x02d7;
	ret->tail = 0;

	ret->data = malloc(sizeof(struct block));
	ret->data->id = 1;
	ret->data->data = NULL;

	char xxx[1024];

	xxx[0] = 0x05;
	block_addchild(ret->data, "1", xxx, 1);

	xxx[0] = 0;
	xxx[1] = 0;
//	xxx[2] = 0;
//	xxx[3] = 0;
	*(unsigned short *)(xxx+2) = htons(0); // job nr.
	xxx[4] = 0xc5;//// same
	xxx[5] = 0xac;//// same
//	same asi jen pro login
//	*(unsigned short *)(xxx+4) = htons(ret->unk3);
	xxx[6] = 0x45;//
	xxx[7] = 0x01;//
	xxx[8] = 0x20;//
	xxx[9] = 0x04;
	block_addchild(ret->data, "2", xxx, 0x0a);

//	block_addchild(ret->data, "4-1", "TACPC", 5);

//	memset(xxx, 0xff, 0x01b8);
//	block_addchild(ret->data, "4-3-1", xxx, 0x01b8);

//	memset(xxx, 0x00, 2);
//	block_addchild(ret->data, "4-3-2-1", xxx, 2);

//	block_addchild(ret->data, "4-3-2-2", "PATR", 4);
	
	//block_addchild(pack->data, "4-3-2-3", "061101", 6);
	//block_addchild(pack->data, "4-3-2-4", "220740", 6);

/*	memset(xxx, 0x00, 0x2e);
	time_t t;
	struct tm *lt;
	t = time(NULL);
	lt = localtime(&t);
	strftime(xxx, 20, "%y%m%d%H%M%S", lt);
	xxx[0x2c] = 0x00;
	xxx[0x2d] = 0x06;
	block_addchild(ret->data, "4-3-2-3", xxx, 0x2e);

	memset(xxx, 0x00, 1);
	block_addchild(ret->data, "4-3-2-5", xxx, 1);
*/
	block_addchild(ret->data, "6", "DISPALARM;", 10);
	return ret;
}


struct packet *login_packet() {
	struct packet *ret = malloc(sizeof(struct packet));
	ret->family = 0xf1;
	ret->unk1 = 0xe0;
	ret->dir = 0x04;
	ret->pltype = 0x00;
	ret->connid = 0xe15a;
	ret->subseq = 0;
	ret->unk2 = 0;
	ret->unk3 = 0x06d8;//// same
	ret->tail = 0;

	ret->data = malloc(sizeof(struct block));
	ret->data->id = 1;
	ret->data->data = NULL;

	char xxx[1024];

	xxx[0] = 0x01;
	block_addchild(ret->data, "1", xxx, 1);

	xxx[0] = 0;
	xxx[1] = 0;
//	xxx[2] = 0;
//	xxx[3] = 0;
	*(unsigned short *)(xxx+2) = htons(0); // job nr.
	xxx[4] = 0x01;//// same
	xxx[5] = 0x0d;//// same
//	same asi jen pro login
//	*(unsigned short *)(xxx+4) = htons(ret->unk3);
	xxx[6] = 0x45;//
	xxx[7] = 0x01;//
	xxx[8] = 0x20;//
//	xxx[9] = 0x03;
	block_addchild(ret->data, "2", xxx, 0x09);

	block_addchild(ret->data, "4-1", "ENM", 3);

	memset(xxx, 0xff, 0x01b8);
	block_addchild(ret->data, "4-3-1", xxx, 0x01b8);

	memset(xxx, 0x00, 2);
	block_addchild(ret->data, "4-3-2-1", xxx, 2);

	block_addchild(ret->data, "4-3-2-2", "ALI1", 4);
	
	//block_addchild(pack->data, "4-3-2-3", "061101", 6);
	//block_addchild(pack->data, "4-3-2-4", "220740", 6);
/*
	memset(xxx, 0x00, 0x2e);
	time_t t;
	struct tm *lt;
	t = time(NULL);
	lt = localtime(&t);
	strftime(xxx, 20, "%y%m%d%H%M%S", lt);
	xxx[0x2c] = 0x00;
	xxx[0x2d] = 0x06;
	block_addchild(ret->data, "4-3-2-3", xxx, 0x2e);
*/

	xxx[0] = 0x95;
	xxx[1] = 0x02;
	xxx[2] = 0x6e;
	xxx[3] = 0x55;
	xxx[4] = 0x12;
	xxx[5] = 0x55;
	xxx[6] = 0x97;
	xxx[7] = 0xe1;
	xxx[8] = 0x33;
	xxx[9] = 0x6e;
	xxx[10] = 0x43;
	xxx[11] = 0xa7;
	xxx[12] = 0xb3;
	xxx[13] = 0x53;
	xxx[14] = 0x07;
	xxx[15] = 0x75;
	xxx[16] = 0x41;
	xxx[17] = 0x7b;
	xxx[18] = 0x0c;
	xxx[19] = 0x06;
	xxx[20] = 0x3a;
	xxx[21] = 0xa8;
	xxx[22] = 0x7d;
	xxx[23] = 0xd5;
	xxx[24] = 0x09;
	xxx[25] = 0x00;

	block_addchild(ret->data, "4-3-2-3", xxx, 0x1a);

	memset(xxx, 0x00, 1);
	block_addchild(ret->data, "4-3-2-5", xxx, 1);

	return ret;
}

int main() {
	int res = 0;

	struct sockaddr_x25 bind_addr, dest_addr;
	bzero(&bind_addr, sizeof(bind_addr));
	bzero(&dest_addr, sizeof(dest_addr));

	bind_addr.sx25_family = AF_X25;
	dest_addr.sx25_family = AF_X25;
	strcpy(bind_addr.sx25_addr.x25_addr, "10000001");
	strcpy(dest_addr.sx25_addr.x25_addr, "10000002");

	int sock = socket(AF_X25, SOCK_SEQPACKET, 0);
	if (sock < 0) {
		perror("socket");
		return 1;
	}

	int on = 1;
	setsockopt(sock, SOL_SOCKET, SO_DEBUG, &on, sizeof(on));

	unsigned long facil_mask = (
//	X25_MASK_REVERSE
//	| X25_MASK_THROUGHPUT
	X25_MASK_PACKET_SIZE
	| X25_MASK_WINDOW_SIZE
	| X25_MASK_CALLING_AE
	| X25_MASK_CALLED_AE);

	struct x25_subscrip_struct subscr;
	int extended = 0;
	subscr.global_facil_mask = facil_mask;
	subscr.extended = extended;
	strcpy(subscr.device, "x25tap0");
	res = ioctl(sock, SIOCX25SSUBSCRIP, &subscr);
	if (res < 0) {
		perror("subscr");
		return 1;
	}

	struct x25_facilities fac;
	fac.winsize_in = 7;
	fac.winsize_out = 7;
	fac.pacsize_in = 10;
	fac.pacsize_out = 10;
	fac.throughput = 0xdd;
	fac.reverse = 0x80;

	res = ioctl(sock, SIOCX25SFACILITIES, &fac);
	if (res < 0) {
		perror("fac");
		return 1;
	}

	unsigned char tmp1[] = {0x36, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x01, 0x30, 0x1f};
	unsigned char tmp2[] = {0x36, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x02, 0x10, 0x1f};
	struct x25_dte_facilities dtefac;
	dtefac.calling_len = 20;
	dtefac.called_len = 20;
	memcpy(&dtefac.calling_ae, &tmp1, 10);
	memcpy(&dtefac.called_ae, &tmp2, 10);

	res = ioctl(sock, SIOCX25SDTEFACILITIES, &dtefac);
	if (res < 0) {
		perror("dtefac");
		return 1;
	}

	res = bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr));
	if (res < 0) {
		perror("bind");
		return 1;
	}

	res = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
	if (res < 0) {
		perror("connect");
		return 1;
	} else {
		printf("connect ok\n");
	}

	struct packet *pack;
	pack = login_packet();
//	pack = command_packet();

	packet_print(pack);

	char buf[32000];
	int l = packet_serialize(pack, buf);
	write(sock, buf, l);

	//packet_delete(pack);

//pack = line_to_packet();
//struct block *b = block_getchild(pack->data, "4-3-2-3");
//memset(b->data, 0, 46);
//b = block_getchild(pack->data, "2");
//b->data = realloc(b->data, 10);
//b->len = 10;
//packet_print(pack);
//int l = packet_serialize(pack, buf);
//write(sock, buf, l);
//packet_delete(pack);

sleep(2);
pack = command_packet();
l = packet_serialize(pack, buf);
write(sock, buf, l);
//packet_delete(pack);

	int r = 0;
	for (;;) {
		r = read(sock, buf, 32000);
		printf("read: %d\n", r);

		if (r <= 0) break;

		struct packet *p = packet_deserialize(buf);
		packet_print(p);
		packet_delete(p);
	}

	close(sock);

	return 0;
}
