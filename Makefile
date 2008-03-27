###################################
#                                 #
#   Edit following defs to your   #
#   configuratuon and needs.      #
#                                 #
###################################

#
# Where to install ewterm. Executables will be installed into
# $(INSTALLDIR), links will be set to $(INSTALLDIR)/bin.
# To $(LIBDIR) we install configuration,
# phonebook etc. (i.e., contents of the lib directory of
# distribution.
#

INSTALLDIR = /opt
LIBDIR = $(INSTALLDIR)

#
# Select one of the two following defines if your system supports serial
# locks. In the other case, comment out all three ones.
# Warning: It MAY NOT contain trailing slash !
#
# The /var/lock directory is suggested by FSSTND.
# The /var/spool/uucp directory is used in many older distributions.
#

# LOCKDIR = /var/spool/uucp
LOCKDIR = /var/lock
# LOCKDIR = /home/pasky/lock
LOCKDEF = -DLOCKDIR=\"$(LOCKDIR)\"

#
# Set size of ewrecv's history - ewrecv will cache this amount of lines,
# which will burst to ewterm at connect time.
#

HISTLEN = -DHISTLEN=10000

#
# Logfile. Define if you want to log all commands
#
# LOGFILE = /tmp/ewterm.log
# LOGDEF = -DLOGFILE=\"$(LOGFILE)\"

#
# C definitions. Note that we strip executables when installing.
#

CC   = gcc
CFLAGS = -g -Wall -O2 -fomit-frame-pointer -DEWDIR=\"$(LIBDIR)/ewterm\" $(LOCKDEF) $(HISTLEN)
#CFLAGS = -DDEBUG -Wall -O2 -fomit-frame-pointer -DEWDIR=\"$(LIBDIR)/ewterm\" $(LOCKDEF) $(HISTLEN)
#-O2
###LDFLAGS =-s

############################
#                          #
#   END OF CONFIGURATION   #
#                          #
############################

# This will try to recognize where to include the ncurses include files
# Stolen from lxdialog utility Makefile from the Linux kernel sources

ifeq (/usr/include/ncurses/ncurses.h, $(wildcard /usr/include/ncurses/ncurses.h))
	CFLAGS += -I/usr/include/ncurses -DNCURSES="<ncurses.h>"
else
ifeq (/usr/include/ncurses/curses.h, $(wildcard /usr/include/ncurses/curses.h))
	CFLAGS += -I/usr/include/ncurses -DNCURSES="<ncurses/curses.h>"
else
ifeq (/usr/include/ncurses.h, $(wildcard /usr/include/ncurses.h))
	CFLAGS += -DNCURSES="<ncurses.h>"
else
	CFLAGS += -DNCURSES="<curses.h>"
endif
endif
endif


RM   = rm -f

EWTERM_SRCS = bfu.c buffer.c colors.c edit.c ewterm.c exch.c frq.c gstr.c help.c history.c lists.c menu.c options.c forms.c formshlp.c formstk.c parseforms.tab.c lex.yy.c iproto.c mml.c sendfile.c md5.c filter.c
EWTERM_OBJS = $(EWTERM_SRCS:.c=.o)

all: .deps mkforms ewterm ewrecv ewrecv_serial ewalarm ewcmd

ewcmd: ewcmd.o iproto.o md5.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o ewcmd ewcmd.o iproto.o md5.o
	
ewalarm: ewalarm.o iproto.o md5.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o ewalarm ewalarm.o iproto.o md5.o

ewterm: $(EWTERM_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o ewterm $(EWTERM_OBJS) -lpanel -lncurses -lfl


ewrecv: ewrecv.o logging.o iproto.o md5.o x25_packet.o x25_block.o x25_utils.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o ewrecv ewrecv.o logging.o iproto.o md5.o x25_packet.o x25_block.o x25_utils.o

ewrecv_serial: ewrecv_serial.o logging.o iproto.o md5.o x25_packet.o x25_block.o x25_utils.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o ewrecv_serial ewrecv_serial.o logging.o iproto.o md5.o x25_packet.o x25_block.o x25_utils.o

mkforms: mkforms.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o mkforms mkforms.o


scanforms.l: parseforms.tab.h
parseforms.tab.h: parseforms.tab.c
parseforms.tab.o: parseforms.tab.c
parseforms.tab.c: parseforms.y
	bison -d parseforms.y
parseforms.y: forms.h

lex.yy.o: lex.yy.c
lex.yy.c: scanforms.l
	flex scanforms.l


clean:
	$(RM) *.o ewalarm ewterm ewrecv ewrecv_serial ewcmd mkforms forms *~ lex.yy.c parseforms.tab.*


install: all

#
# Now we will make ewterm setuid and executable just to group mdterm
#

	./InstallScript $(INSTALLDIR) $(LOCKDIR)


.deps:	*.c *.l *.y
	@gcc -MM *.c >.deps

depend:	.deps

sinclude .deps
