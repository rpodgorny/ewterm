package OpenSwitch::EWTerm;

use strict;
use vars qw($VERSION @ISA @EXPORT @EXPORT_OK);

require Exporter;
require DynaLoader;
require AutoLoader;

@ISA = qw(Exporter AutoLoader);
# Items to export into callers namespace by default. Note: do not export
# names by default without a very good reason. Use EXPORT_OK instead.
# Do not simply export all your public functions/methods/constants.
@EXPORT_OK = qw(
    init handshake %OpenSwitch::EWTerm::handlers sendp askp test_char
    );
$VERSION = '1.0';

# Preloaded methods go here.

use FileHandle;
use IO::Socket;
use IO::Handle;
use Net::hostent;
use POSIX ":sys_wait_h";
use Fcntl;

my ($dc1, $dc2, $dc3, $dc4, $state, $packet);

$dc1=chr(17);
$dc2=chr(18);
$dc3=chr(19);
$dc4=chr(20);

$state=$dc1;
$packet="";

sub dummy
 {
  print "";
 }

sub _askver
 {
  sendp(0xff,"2.3a");
 }

sub _askusr
 {
  sendp(0x01,$ENV{"USER"}?$ENV{"USER"}:"UNKNOWN");
 }

%OpenSwitch::EWTerm::handlers =
 (
  RecvChar => \&dummy,
  Write => \&dummy,
  BurstStart => \&dummy,
  BurstEnd => \&dummy,
  SENDVersion => \&dummy,
  SENDNotify => \&dummy,
  SENDUnknownASK => \&dummy,
  SENDForwardMode => \&dummy,
  SENDUserConnect => \&dummy,
  SENDUserDisconnect => \&dummy,
  SENDPromptStart => \&dummy,
  SENDPromptEnd => \&dummy,
  SENDLoginError => \&dummy,
  SENDLoginSuccess => \&dummy,
  SENDLogout => \&dummy,
  SENDJobEnd => \&dummy,
  SENDMaskNumber => \&dummy,
  SENDHeader => \&dummy,
  SENDPrivMsg => \&dummy,
  ASKVersion => \&_askver,
  ASKUser => \&_askusr,
 );

sub sendp
 {
  my ($op,$packet) = @_;
  my ($p) = sprintf('%s%s%s%s',$dc2,chr($op),$packet,$dc1);
  $OpenSwitch::EWTerm::handlers{"Write"}($p);
 }

sub askp
 {
  my ($op,$packet) = @_;
  my ($p) = sprintf('%s%s%s%s',$dc3,chr($op),$packet,$dc1);
  $OpenSwitch::EWTerm::handlers{"Write"}($p);
 }


sub process_dc
 {
  my ($c) = @_; my $ch;
  ($ch, $packet) = ($packet =~ /^(.)(.*)$/);
  $ch = ord($ch) if (defined $ch);

  if (ord($state) == ord($dc2))
   {
    if ($ch == 0xff)
     { # version
      $packet =~ s/[,:-].*$//;
      $OpenSwitch::EWTerm::handlers{'SENDVersion'}($packet);
     }
    elsif ($ch == 0x2)
     { # notify
      $OpenSwitch::EWTerm::handlers{'SENDNotify'}($packet);
     }
    elsif ($ch == 0x80)
     { # unknown ask
      $OpenSwitch::EWTerm::handlers{'SENDUnknownASK'}($packet);
     }
    elsif ($ch == 0x04)
     { # forward mode
      $OpenSwitch::EWTerm::handlers{'SENDForwardMode'}($packet);
     }
    elsif ($ch == 0x05)
     { # user connect
      my ($uname, $host, $id) = ($packet =~ /^(.+?)@(.+?):(\d+)([,.].*)?$/);
      my ($self) = ($uname =~ s/^(\!)//);
      $self = 0 unless (defined $self);
      $OpenSwitch::EWTerm::handlers{'SENDUserConnect'}($self, $uname, $host, $id);
     }
    elsif ($ch == 0x06)
     { # user disconnect
      my ($uname, $host, $id) = ($packet =~ /^(.+?)@(.+?):(\d+)[,.].*$/);
      $OpenSwitch::EWTerm::handlers{'SENDUserDisconnect'}(0, $uname, $host, $id);
     }
    elsif ($ch == 0x40)
     { # prompt start
      $OpenSwitch::EWTerm::handlers{'SENDPromptStart'}();
     }
    elsif ($ch == 0x41)
     { # prompt end
      my ($type, $job) = ($packet =~ /^(.)(.*)?$/);
      $OpenSwitch::EWTerm::handlers{'SENDPromptEnd'}($type, $job);
     }
    elsif ($ch == 0x42)
     { # login error
      $OpenSwitch::EWTerm::handlers{'SENDLoginError'}();
     }
    elsif ($ch == 0x43)
     { # login success
      $OpenSwitch::EWTerm::handlers{'SENDLoginSuccess'}();
     }
    elsif ($ch == 0x44)
     { # logout
      $OpenSwitch::EWTerm::handlers{'SENDLogout'}();
     }
    elsif ($ch == 0x45)
     { # job end
      $packet =~ s/[,.:@;-].*$//;
      $OpenSwitch::EWTerm::handlers{'SENDJobEnd'}($packet);
     }
    elsif ($ch == 0x46)
     { # mask num
      $packet =~ s/[,.:@;-].*$//;
      $OpenSwitch::EWTerm::handlers{'SENDMaskNumber'}($packet);
     }
    elsif ($ch == 0x47)
     { # header
      my ($job,$omt,$username,$exchange) = ($packet =~ /^(.*?),(.*?),(.*?),(.*?)/);
      $OpenSwitch::EWTerm::handlers{'SENDHeader'}($job,$omt,$username,$exchange);
     }
    elsif ($ch == 0x03)
     { # privmsg
      my ($uname, $host, $id, $msg) = ($packet =~ /^(.*?)@(.*?):(\d+).*?=(.*)$/);
      $OpenSwitch::EWTerm::handlers{'SENDPrivMsg'}($uname, $host, $id, $msg);
     }
   }
  elsif (ord($state) == ord($dc3))
   {
    if ($ch == 0xff)
     { # version
      $OpenSwitch::EWTerm::handlers{'ASKVersion'}();
     }
    elsif ($ch == 0x01)
     { # user
      $OpenSwitch::EWTerm::handlers{'ASKUser'}();
     }
    else
     { # the rest can't be received by ewterm-like sw
      sendp(0x80,$packet);
     }
   }

  $state = $c;
  $packet = "";
 }



sub init
 {
  my ($host,$port) = @_; my $handle;
  $handle = IO::Socket::INET->new(Proto=>"tcp",PeerAddr=>$host,PeerPort=>$port);
  $handle->autoflush(1);
  $|=1;
  unless (defined $handle and $handle)
   {
    print("Can't establish the TCP connection - $!");
    return undef;
   }

  return $handle;
 }

sub handshake
 {
  $OpenSwitch::EWTerm::handlers{'Write'}("$dc1");
 }

sub testchar
 {
  my ($ch) = @_;
  if (ord($ch) >= ord($dc1) and ord($ch) <= ord($dc4))
   {
#   print "pkdc $ch\n";
    process_dc($ch);
   }
  elsif (ord($state) == ord($dc2) or ord($state) == ord($dc3))
   {
#   print "pkstore $ch\n";
    $packet .= $ch;
   }
  else
   {
#   print "mbrvc $ch\n";
    if (ord($ch) == 14)
     { # burst start
      $OpenSwitch::EWTerm::handlers{'BurstStart'}();
      return;
     }
    if (ord($ch) == 15)
     { # burst end
      $OpenSwitch::EWTerm::handlers{'BurstEnd'}();
      return;
     }

    $OpenSwitch::EWTerm::handlers{'RecvChar'}($ch);
   }
 }

=head1 example

my $h;

sub wr
 {
  my ($p) = @_;
  print $h "$p";
  print STDERR "$p";
 }

sub pr
 {
  print $_[0] if (ord($_[0]) == 9 or ord($_[0]) == 10 or ord($_[0]) >= 32);
 }

sub sv
 {
  print "\n::version ",$_[0],"\n";
 }

sub bs
 {
  print "\nBURST START\n\n";
 }

sub be
 {
  print "\nBURST END\n\n";
  askp(0xff,"");
 }

$h = init($host,$port);
unless (defined $h and $h) { print "..\n"; exit 1; }

$OpenSwitch::EWTerm::handlers{'Write'} = \&wr;
$OpenSwitch::EWTerm::handlers{'RecvChar'} = \&pr;
$OpenSwitch::EWTerm::handlers{'SENDVersion'} = \&sv;
$OpenSwitch::EWTerm::handlers{'BurstStart'} = \&bs;
$OpenSwitch::EWTerm::handlers{'BurstEnd'} = \&be;

handshake();

while(1){my$x;unless(read($h,$x,1)){print"E\n";exit 2;}testchar($x);}

=cut

# Autoload methods go after =cut, and are processed by the autosplit program.

1;
__END__
# Below is the documentation for the module.

=head1 NAME

Text::Iconv - Perl interface to iconv() codeset conversion function

=head1 SYNOPSIS

use Text::Iconv;
$converter = Text::Iconv->new("fromcode", "tocode");
$converted = $converter->convert("Text to convert");

=head1 DESCRIPTION

The B<Text::Iconv> module provides a Perl interface to the iconv()

=head1 ERRORS

=head1 NOTES

=head1 AUTHOR

=head1 SEE ALSO

ewterm(1), ewrecv(8)

=cut
