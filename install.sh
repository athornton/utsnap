: 'install.sh -- put the nap driver into the UTS kernel sources and build.'
:
: 'Run as root, from the directory holding nap.c and nap.h:'
:	'sh install.sh'
:
: 'It edits four kernel files, keeping each original as FILE.nap-orig,'
: 'builds /usr/src/sys/conf/uts, and stops.  It does NOT install the'
: 'kernel or make /dev/nap -- see INSTALL.md for those steps.  Running it'
: 'twice is safe: every edit starts again from the .nap-orig copy.'
:
: 'Written for the 1981 UTS tools, each of which differs from what you'
: 'would expect.  sh has no functions, and these colon lines are real'
: 'commands, so every comment is quoted.  grep -c names the file even'
: 'when given only one, and dies on stdin, so counts use wc -l.  awk'
: 'cannot assign $0, and wants $0 ~ /re/ rather than a bare /re/ in a'
: 'compound pattern.  ld writes an output file even when the link fails,'
: 'so success is judged by the exit status of make.  The patterns avoid'
: 'exact whitespace, which a 3270 cannot show you.'

S=/usr/src/sys
here=`pwd`

if test ! -f nap.c -o ! -f nap.h
then
	echo "install.sh: run me where nap.c and nap.h are"; exit 1
fi
if test ! -d $S/conf
then
	echo "install.sh: no kernel source in $S"; exit 1
fi

: 'Keep the originals once, and always edit from them.'
for f in sys/clock.c sys/machdep.c conf/conf.c dev/Makefile
do
	if test ! -f $S/$f.nap-orig
	then
		cp $S/$f $S/$f.nap-orig || exit 1
	fi
done

: 'The driver and its header.  /usr/src and /usr are different file'
: 'systems (dsk330 and dsk110), so the user copy of nap.h is a copy,'
: 'not a link.'
cp nap.c $S/dev/nap.c || exit 1
cp nap.h $S/h/nap.h || exit 1
cp nap.h /usr/include/nap.h || exit 1

: 'clock.c: the comparator interrupt asks nap first, and tick() re-arms'
: 'the comparator through clksckc() so that nap deadlines are honoured.'
sed -e '/case CLKCOMP:/,/break;/s/tick();/if (napclock()) tick();/' \
    -e 's/a_sckc(/clksckc(/' \
    $S/sys/clock.c.nap-orig > $S/sys/clock.c
if test `grep 'if (napclock()) tick();' $S/sys/clock.c | wc -l` -ne 1 \
     -o `grep 'clksckc(' $S/sys/clock.c | wc -l` -ne 1 \
     -o `grep 'a_sckc(' $S/sys/clock.c | wc -l` -ne 0
then
	echo "install.sh: clock.c edit failed"; exit 1
fi

: 'machdep.c: clkstart() arms the first tick the same way.'
sed -e 's/a_sckc(/clksckc(/' $S/sys/machdep.c.nap-orig > $S/sys/machdep.c
if test `grep 'clksckc(' $S/sys/machdep.c | wc -l` -ne 1 \
     -o `grep 'a_sckc(' $S/sys/machdep.c | wc -l` -ne 0
then
	echo "install.sh: machdep.c edit failed"; exit 1
fi

: 'conf.c: declare the entry points, and APPEND a cdevsw row after the'
: 'last one (sy), so every existing major number stays where it is.'
awk '{ print }
$0 ~ /func/ && $0 ~ /ldsopen, ldsclose/ { print "func   napopen, napioctl;" }
$0 ~ /syopen, nulldev, syread, sywrite, sysioctl, nulldev, 0, 0,/ {
	print "        napopen, nulldev, nodev, nodev, napioctl, nulldev, 0, 0,"
}' $S/conf/conf.c.nap-orig > $S/conf/conf.c
if test `grep 'napopen' $S/conf/conf.c | wc -l` -ne 2
then
	echo "install.sh: conf.c edit failed"; exit 1
fi

: 'dev/Makefile: build nap.o into libdev.a.  The OBJ and LOUT lists'
: 'are the only lines naming vmcf without a colon.'
awk '{ line = $0 }
$0 ~ /vmcf\.o/ && $0 !~ /:/ { line = line " nap.o" }
$0 ~ /vmcf\.L/ && $0 !~ /:/ { line = line " nap.L" }
{ print line }
END { print "nap.o nap.L:          nap.c     ../h/param.h ../h/systm.h ../h/dir.h ../h/user.h ../h/conf.h ../h/nap.h" }' \
    $S/dev/Makefile.nap-orig > $S/dev/Makefile
if test `grep 'nap\.o' $S/dev/Makefile | wc -l` -ne 2 \
     -o `grep 'nap\.L' $S/dev/Makefile | wc -l` -ne 2
then
	echo "install.sh: dev/Makefile edit failed"; exit 1
fi

: 'Report the major number: the napopen row of cdevsw, counted from 0.'
major=`awk 'f == 1 && $0 ~ /napopen/ { m = n }
f == 1 && $0 ~ /,/ { n++ }
$0 ~ /struct cdevsw cdevsw/ { f = 1 }
END { print m }' $S/conf/conf.c`
echo "install.sh: nap is character major $major"

: 'Build.'
cd $S/conf || exit 1
rm -f uts
if make uts > $here/build.log 2>&1
then
	cat $here/build.log
else
	cat $here/build.log
	echo "install.sh: make failed; see build.log"; exit 1
fi
if grep 'Undefined' $here/build.log > /dev/null
then
	echo "install.sh: undefined symbols; see build.log"; exit 1
fi
if test ! -f uts
then
	echo "install.sh: no kernel built; see build.log"; exit 1
fi
nm uts > $here/nm.log
for sym in napopen napioctl napclock clksckc napslot
do
	if grep "$sym" $here/nm.log > /dev/null
	then
		:
	else
		echo "install.sh: kernel lacks $sym; see nm.log"; exit 1
	fi
done
ls -l uts
echo "install.sh: built $S/conf/uts with nap at major $major"
