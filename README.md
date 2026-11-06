# utsnap

Sub-second sleeps for Amdahl UTS under VM/370, implemented as a
character-mode device driver and a userspace library. A port of
v7nap, the same driver for Unix v7 on the PDP-11.

## Rationale

There is no way for UTS user code to pause less than a second.
`sleep(3)` relies on the combination of `alarm(2)` and `pause(2)`, and
`alarm` has a granularity of seconds.

Thus, unless you have a nap driver of some sort, you cannot poll
anything with a frequency of less than a second, unless you resort to
busy-waiting. In order to make a highly responsive 3270 terminal
application, polling the user's terminal more frequently than once a
second is required.

## How

Unlike v7, the UTS kernel has no sub-second clock to expose: its `HZ`
is 1, and its only timer interrupt is the S/370 clock comparator, set
one second ahead at a time. So the driver shares the clock comparator
with the kernel's once-a-second `tick()`, loading it with whichever
comes first, the next tick or the earliest nap deadline, timed from
the TOD clock. That costs three one-line changes to `clock.c` and
`machdep.c` besides the driver itself. See [INSTALL.md](INSTALL.md).

The user interface is the same as v7nap's: `nap(ticks)` and `naphz()`,
with 60 ticks per second, so programs move between the two unchanged.
