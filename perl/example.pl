#!/usr/bin/perl -w

use lib qw(.);

use FileHandle;
use IO::Socket;
use IO::Handle;
use Net::hostent;
use POSIX ":sys_wait_h";
use Fcntl;

use OpenSwitch::EWTerm;

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
  OpenSwitch::EWTerm::askp(0xff,"");
 }

$h = OpenSwitch::EWTerm::init("localhost",7880);
unless (defined $h and $h) { print "..\n"; exit 1; }

$OpenSwitch::EWTerm::handlers{'Write'} = \&wr;
$OpenSwitch::EWTerm::handlers{'RecvChar'} = \&pr;
$OpenSwitch::EWTerm::handlers{'SENDVersion'} = \&sv;
$OpenSwitch::EWTerm::handlers{'BurstStart'} = \&bs;
$OpenSwitch::EWTerm::handlers{'BurstEnd'} = \&be;

OpenSwitch::EWTerm::handshake();

while(1){my$x;unless(read($h,$x,1)){print"E\n";exit 2;}OpenSwitch::EWTerm::testchar($x);}
