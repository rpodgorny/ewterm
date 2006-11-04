#include <sys/socket.h>
#include <linux/x25.h>
#include <string.h>
#include <strings.h>

int main() {
	struct sockaddr_x25 bind_addr, dest_addr;
	bzero(&bind_addr, sizeof(bind_addr));
	bzero(&dest_addr, sizeof(dest_addr));

	bind_addr.sx25_family = AF_X25;
	dest_addr.sx25_family = AF_X25;
	strcpy(bind_addr.sx25_addr.x25_addr, "10000001");
	strcpy(dest_addr.sx25_addr.x25_addr, "10000002");

	int sock = socket(AF_X25, SOCK_SEQPACKET, 0);

	int res = bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr));

	res = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));

	close(sock);

	return 0;
}
