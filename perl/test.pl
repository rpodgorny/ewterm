use Socket;
use IO::Handle;
use OpenSwitch::EWTerm qw(Attach DoWrite DoRead Read Write);

my $SOCK = IO::Handle->new();
$SOCK->autoflush(1);
$|=1;

$iaddr = inet_aton("localhost") || die "Server not found: $remote";
$paddr = sockaddr_in(7880, $iaddr);
$proto = getprotobyname('tcp');

socket($SOCK, PF_INET, SOCK_STREAM, $proto) || die "Cannot create socket: $!";
connect($SOCK, $paddr) || die "Cannoct connect: $!";

Attach(Fd => $SOCK->fileno, SENDVersion => SENDVersion);

while (1) {
  DoWrite();
  DoRead();
}

sub SENDVersion {
  my ($xxxxx, $v) = @_;
  print"Got version $v! :)\n";
}
