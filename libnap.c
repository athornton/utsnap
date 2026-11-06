/*
 * nap(), naphz() -- user-space wrappers for /dev/nap.  K&R C, UTS libc.
 * See nap.3.
 */

#include <nap.h>

#define	FIOCLEX	(('f'<<8)|1)	/* as in sgtty.h and the kernel's tty.h */

extern int errno;

static int napfd = -1;

/*
 * Open /dev/nap once and cache it.  Marked close-on-exec so children do
 * not silently inherit a descriptor they did not ask for -- a process
 * has only NOFILE of them.  ioctl() in dev/tty.c handles FIOCLEX itself,
 * before the driver is consulted, so it works on any descriptor.
 */
static
napinit()
{

	if (napfd >= 0)
		return(0);
	if ((napfd = open("/dev/nap", 0)) < 0)
		return(-1);
	ioctl(napfd, FIOCLEX, (char *)0);
	return(0);
}

nap(ticks)
int ticks;
{
	int t;

	if (napinit() < 0)
		return(-1);
	t = ticks;
	return(ioctl(napfd, NAPSLP, &t));
}

naphz()
{
	int hz;

	if (napinit() < 0)
		return(-1);
	if (ioctl(napfd, NAPGHZ, &hz) < 0)
		return(-1);
	return(hz);
}
