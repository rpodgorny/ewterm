#include <stdio.h>
#include <sys/socket.h>
#include <linux/sockios.h>
#include <linux/x25.h>
#include <string.h>
#include <strings.h>

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
	}

	write(sock, tmp1, 11);

	close(sock);

	return 0;
}
