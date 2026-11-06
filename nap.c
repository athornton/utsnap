/*
 * nap -- sub-second delays for user processes, UTS version.
 *
 * UTS gives user code no delay shorter than one second: sleep(3) is
 * alarm(2) plus pause(2), and alarm's granularity is seconds
 * (sys4.c sets p_clktim, which tick() in clock.c decrements once per
 * second).  Unlike V7, the UTS kernel has no finer clock either:
 * param.h sets HZ to 1, there is no timeout() and no callout list, and
 * the only timer interrupt is the clock comparator, which tick()
 * re-arms one second ahead each time it runs.
 *
 * The hardware is far better than that.  The S/370 TOD clock counts
 * microseconds (bit 51), and the clock comparator interrupts when the
 * TOD clock passes any value we load into it.  So this driver shares
 * the one comparator with tick(): it is always set to whichever comes
 * first, the next once-a-second tick or the earliest nap deadline.
 * That takes three one-line changes outside this file (see INSTALL.md):
 *
 *	clock.c, extrn():	case CLKCOMP: if (napclock()) tick();
 *	clock.c, tick():	a_sckc(...)  becomes  clksckc(...)
 *	machdep.c, clkstart():	a_sckc(...)  becomes  clksckc(...)
 *
 * Usage, sleeping 3 ticks (50 ms at NAPHZ 60):
 *
 *	#include <nap.h>
 *	int fd, ticks;
 *	fd = open("/dev/nap", 0);
 *	ticks = 3;
 *	ioctl(fd, NAPSLP, &ticks);
 *
 * Install as /usr/src/sys/dev/nap.c; see nap.3 for the user side and
 * INSTALL.md for the conf.c, Makefile and mknod steps.
 */

#include "../h/param.h"
#include "../h/systm.h"
#include "../h/dir.h"
#include "../h/user.h"
#include "../h/conf.h"
#include "../h/nap.h"

/*
 * The nap is deliberately NOT interruptible.
 *
 * sleep() in slp.c takes the interruptible path only when its priority
 * argument is strictly greater than PZERO; that path checks issig()
 * and may leave via resume(u.u_qsav) -- a non-local jump straight out
 * of the driver, which would leave our slot marked busy forever.
 * Sleeping at PZERO takes the other branch -- plain swtch(), no
 * issig(), no longjmp -- so we always return normally and tidy up.
 * The price is that a napping process ignores signals until the nap
 * ends, which is why NAPMAXT (nap.h) keeps the maximum nap short.
 */
#define	NAPPRI	PZERO

/*
 * Each in-flight nap holds one slot.  Unlike V7 there is no shared
 * callout table to overflow (V7's timeout() panics when its 20 entries
 * fill), so this is only a table size; a full table gives EAGAIN.
 */
#define	NNAP	16

/*
 * TOD clock units: bit 51 is one microsecond, so one second is
 * 1000000<<12.  One nap tick is that over NAPHZ, truncated, which is
 * short by under one part in 10^7 -- far below anything VM delivers.
 */
#define	TODSEC	(1000000L<<12)
#define	TODTICK	(TODSEC / NAPHZ)

/*
 * "TOD a is at or past TOD b."  Compare by difference, never directly:
 * tod_t is a signed long and the TOD clock's top bit has been set since
 * 1971, so every current TOD value is negative.  (ctime() in clock.c
 * masks that bit off for the same reason.)
 */
#define	TODGE(a, b)	((long)((a) - (b)) >= 0)

struct	napslot {
	char	n_busy;		/* slot owned by a napping process */
	char	n_wait;		/* deadline not yet reached */
	tod_t	n_due;		/* TOD at which the nap ends */
};
struct	napslot	napslot[NNAP];

tod_t	clkdue;			/* TOD at which tick() wants its interrupt */
int	clkset;			/* clkdue is valid (clkstart() has run) */

/*
 * Load the clock comparator with the earliest of tick()'s next second
 * and every pending nap deadline.  A value already in the past just
 * makes the interrupt happen at once, which is what we want.
 *
 * The UTS kernel runs with I/O and external interrupts masked (the new
 * PSWs in zero.s are x'001c0000'; only idle() and window() open them),
 * so nothing here needs the V7 spl7()/splx() bracketing: the comparator
 * cannot fire between our loading it and the caller's sleep().
 */
napsckc()
{
	register struct napslot *sp;
	tod_t when;
	int have;

	when = clkdue;
	have = clkset;
	for (sp = &napslot[0]; sp < &napslot[NNAP]; sp++)
		if (sp->n_wait && (!have || !TODGE(sp->n_due, when))) {
			when = sp->n_due;
			have = 1;
		}
	if (have)
		a_sckc(when);
}

/*
 * Replaces a_sckc() in tick() and clkstart(): record when the next
 * once-a-second tick is due, then load the comparator.
 */
clksckc(due)
tod_t due;
{

	clkdue = due;
	clkset = 1;
	napsckc();
}

/*
 * Called from extrn() on every clock comparator interrupt, in place of
 * a bare tick().  Ends any naps whose deadline has passed, and returns
 * nonzero when tick() is due -- tick() then re-arms the comparator
 * itself, through clksckc().  Otherwise the interrupt was for a nap
 * only, and the comparator is re-armed here.
 */
napclock()
{
	register struct napslot *sp;
	tod_t tod, a_stck();

	tod = a_stck();
	for (sp = &napslot[0]; sp < &napslot[NNAP]; sp++)
		if (sp->n_wait && TODGE(tod, sp->n_due)) {
			sp->n_wait = 0;
			wakeup((caddr_t)sp);
		}
	if (!clkset || TODGE(tod, clkdue))
		return(1);
	napsckc();
	return(0);
}

napopen(dev, flag)
{

	if (minor(dev) != 0) {
		u.u_error = ENXIO;
		return;
	}
}

/*
 * t is plain int, not register: copyin/copyout take its address.
 * copyin and copyout (trans.c) return 1 on a fault and 0 on success.
 */
/* ARGSUSED */
napioctl(dev, cmd, addr, flag)
caddr_t addr;
{
	register struct napslot *sp;
	register int i;
	tod_t a_stck();
	int t;

	switch (cmd) {

	case NAPGHZ:
		t = NAPHZ;
		if (copyout((caddr_t)&t, addr, sizeof(t)))
			u.u_error = EFAULT;
		return;

	case NAPSLP:
		if (copyin(addr, (caddr_t)&t, sizeof(t))) {
			u.u_error = EFAULT;
			return;
		}
		if (t < 0 || t > NAPMAXT) {
			u.u_error = EINVAL;
			return;
		}
		if (t == 0)		/* nothing to wait for */
			return;
		for (sp = &napslot[0]; sp < &napslot[NNAP]; sp++)
			if (sp->n_busy == 0)
				goto found;
		u.u_error = EAGAIN;	/* too many nappers; see NNAP */
		return;
	found:
		sp->n_busy = 1;
		/*
		 * Add, don't multiply: a long multiply compiles to a call
		 * of _llmul, which the kernel does not link (measured --
		 * ld reports it undefined).  At most NAPMAXT additions.
		 */
		sp->n_due = a_stck();
		for (i = t; i > 0; i--)
			sp->n_due += TODTICK;
		sp->n_wait = 1;
		napsckc();
		/*
		 * Loop, as slp.c asks of every caller: a wakeup on our
		 * slot's address is not proof that our deadline passed.
		 */
		while (sp->n_wait)
			sleep((caddr_t)sp, NAPPRI);
		sp->n_busy = 0;
		return;

	default:
		u.u_error = ENOTTY;
		return;
	}
}
