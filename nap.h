/*
 * nap - sub-second delays, measured in nap ticks (1/NAPHZ second).
 *
 * This header is shared by the kernel driver (/usr/src/sys/dev/nap.c)
 * and by user programs.  Install it as /usr/src/sys/h/nap.h and link
 * /usr/include/nap.h to it.  It must therefore not depend on any
 * kernel header.
 *
 * UTS's own clock is no help here: param.h sets HZ to 1, and clock.c
 * takes one clock comparator interrupt per second.  So the nap driver
 * keeps its own time, straight from the S/370 TOD clock, and a nap
 * tick is a unit of the driver's, not of the kernel's.  NAPHZ is 60 so
 * that programs written for the V7 version of /dev/nap (where a tick
 * really was the 60 Hz line clock) run unchanged.  Call naphz() rather
 * than assuming it.
 *
 * The letter 'n' is unused by UTS's other ioctl groups (see
 * /usr/src/sys/h/tty.h and /usr/include/sgtty.h).
 */

#define	NAPSLP	(('n'<<8)|1)	/* arg: int *, number of ticks to sleep */
#define	NAPGHZ	(('n'<<8)|2)	/* arg: int *, gets ticks per second */

#define	NAPHZ	60		/* nap ticks per second */

/*
 * Longest nap accepted by NAPSLP, in ticks.  The sleep is deliberately
 * not interruptible (see nap.c), so this bound is also the longest a
 * napping process can ignore a signal.  Keep it short.  120 ticks is
 * two seconds.
 */
#define	NAPMAXT	120
