/*
 * naptest -- check that /dev/nap actually delays, by how much, and that
 * it sleeps rather than spins.  UTS version.
 *
 * Elapsed time comes from ftime(2).  On UTS that is read straight from
 * the S/370 TOD clock (sys4.c: millitm = TOD microseconds / 1000), so it
 * is genuinely millisecond-accurate -- much finer than a nap tick, and
 * independent of the nap driver's own arithmetic.  time(2) would round
 * every nap to a second and "prove" any length correct.
 *
 * CPU time comes from times(2), and only to tell sleeping from spinning.
 * UTS's times() reports MICROSECONDS (see /usr/include/sys/times.h), not
 * clock ticks as in V7.  A correct nap burns no CPU, so the tms_ figures
 * alone cannot show that a delay happened at all.  The two clocks answer
 * two different questions and both are needed.
 *
 * Under VM/370 the clock comparator interrupt that ends a nap is
 * simulated by CP, and the UTS virtual machine must also be dispatched
 * before the napper can run, so expect lumpier timings than on bare
 * hardware.  Widen the bounds from observed numbers rather than assuming
 * the first failure is a driver bug.
 *
 *	cc -o naptest naptest.c libnap.c
 *	./naptest
 */

#include <nap.h>
#include <sys/types.h>
#include <sys/timeb.h>
#include <sys/times.h>

int tries[] = { 1, 2, 3, 6, 12, 30, 60, 0 };

/*
 * Elapsed milliseconds between two ftime samples.
 *
 * Everything that is multiplied in this file is an int.  UTS's cc
 * cannot multiply two longs (64 bits here): it stops with "No code
 * table for op 133" on user code, and in the kernel the same operation
 * becomes a call to an _llmul that is not linked.  The quantities fit:
 * the longest nap is two seconds.
 */
int
msdiff(a, b)
struct timeb *a, *b;
{
	int secs;

	secs = b->time - a->time;
	return (secs * 1000 + (int)b->millitm - (int)a->millitm);
}

main()
{
	struct timeb f0, f1;
	struct tms c0, c1;
	long cpu_us;
	int want_ms, got_ms, tick_ms;
	int hz, i, want;
	int fail;

	fail = 0;
	if ((hz = naphz()) < 0) {
		printf("naptest: naphz failed; is /dev/nap present?\n");
		exit(1);
	}
	tick_ms = 1000 / hz;
	printf("HZ = %d (%d ms/tick)\n", hz, tick_ms);
	printf("ticks\twant\tgot\tcpu(us)\n");

	for (i = 0; tries[i] != 0; i++) {
		want = tries[i];
		want_ms = want * 1000 / hz;

		ftime(&f0);
		times(&c0);
		if (nap(want) < 0) {
			printf("%d\tnap failed\n", want);
			fail++;
			continue;
		}
		ftime(&f1);
		times(&c1);

		got_ms = msdiff(&f0, &f1);
		cpu_us = (c1.tms_utime - c0.tms_utime)
		       + (c1.tms_stime - c0.tms_stime);
		printf("%d\t%dms\t%dms\t%ld", want, want_ms, got_ms,
			cpu_us);

		/*
		 * A nap waits between want-1 and want ticks (the current
		 * tick is already partly spent), ftime itself is only
		 * tick-accurate, and the process need not be rescheduled the
		 * instant it wakes.  So allow two ticks of slack below and
		 * generous slack above.  Under VM these bounds are a
		 * guess; widen them from observed numbers rather than
		 * assuming the first failure is a driver bug.
		 */
		if (got_ms < want_ms - 2 * tick_ms)
			printf("\tTOO SHORT"), fail++;
		else if (got_ms > want_ms + 3 * tick_ms + want_ms / 4)
			printf("\tTOO LONG"), fail++;
		/*
		 * If CPU time grew with the delay we spun instead of
		 * sleeping -- exactly what this driver exists to avoid.
		 * Only meaningful once the nap is several ticks long.
		 */
		if (want >= 6 && cpu_us >= want_ms * 500)
			printf("\tBUSY-WAITED"), fail++;
		printf("\n");
	}

	/* Bounds: both should be refused, not silently clamped. */
	if (nap(-1) >= 0)
		printf("negative nap accepted\n"), fail++;
	if (nap(NAPMAXT + 1) >= 0)
		printf("oversize nap accepted\n"), fail++;

	if (fail)
		printf("FAIL (%d)\n", fail);
	else
		printf("PASS\n");
	exit(fail ? 1 : 0);
}
