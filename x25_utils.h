#ifndef EW__X25_UTILS_H
#define EW__X25_UTILS_H

#include "x25_packet.h"

struct packet *login_packet(char *, char *);
struct packet *command_packet(char *, int);
struct packet *command_confirmation_packet(unsigned short, unsigned short, unsigned char, char *, int);

#endif
