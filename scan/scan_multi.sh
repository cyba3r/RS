
#mkfifo /tmp/sdr.0
#mkfifo /tmp/sdr.1
#mkfifo /tmp/sdr.2

#mkfifo log.0
#mkfifo log.1
#mkfifo log.2

TERM="xfce4-terminal"

$TERM --geometry=64x61+470+140 --hide-menubar -T term0 -H -e './scan_multi_rs.pl'

$TERM --geometry=160x20+1000+836 --hide-menubar -T term3 -H -e 'cat log.2'
$TERM --geometry=160x20+1000+488 --hide-menubar -T term2 -H -e 'cat log.1'
$TERM --geometry=160x20+1000+140 --hide-menubar -T term1 -H -e 'cat log.0'


