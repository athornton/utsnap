# Installing the `/dev/nap` driver on UTS

Sub-second sleeps for Amdahl UTS (1981) running under VM/370.

UTS has no loadable modules, so this means editing the kernel source,
relinking the kernel, and re-IPLing the UTS virtual machine.

## Why this exists

UTS user code has no delay shorter than a second. `sleep(3)` is
`alarm(2)` plus `pause(2)`, and `alarm` is seconds-granular — `sys4.c`
stores `p_clktim`, which `tick()` in `clock.c` decrements once per
second.

UTS can already **measure** sub-second time: `ftime(2)` reads the S/370
TOD clock directly (`sys4.c`: `millitm` is TOD microseconds / 1000), so
it is millisecond-accurate. But measuring is not sleeping.

## How it differs from the V7 driver

The V7 driver is a thin window onto V7's 60 Hz `timeout()`. **UTS has
nothing of the kind**: `h/param.h` sets `HZ` to **1**, there is no
`timeout()` and no callout list (`h/callo.h` declares one; nothing uses
it), and the only timer interrupt is the **clock comparator**, which
`tick()` re-arms one second ahead each time it runs.

So the UTS driver keeps its own time. Each nap records a TOD-clock
deadline, and the one clock comparator is always loaded with whichever
comes first: the next once-a-second `tick()` or the earliest nap
deadline. That needs three one-line changes to the kernel outside the
driver:

| File | Change |
|---|---|
| `sys/clock.c`, `extrn()` | `case CLKCOMP: tick();` becomes `if (napclock()) tick();` |
| `sys/clock.c`, `tick()` | `a_sckc(...)` becomes `clksckc(...)` |
| `sys/machdep.c`, `clkstart()` | `a_sckc(...)` becomes `clksckc(...)` |

`napclock()` wakes every nap whose deadline has passed and reports
whether `tick()` is due; `clksckc()` records `tick()`'s next deadline
and loads the comparator with the earliest one. Both are in `nap.c`.

Other UTS facts the driver depends on, all read from the source on the
running system:

- **The kernel runs with I/O and external interrupts masked.** The new
  PSWs in `conf/zero.s` are `x'001c0000'`, and interrupts are only
  taken in `idle()` and `window()`. So the V7 driver's `spl7()`/`splx()`
  bracketing is unnecessary (and UTS has no `spl` routines): the
  comparator cannot fire between loading it and `sleep()`.
- `sleep()` in `sys/slp.c` matches V7: `pri <= PZERO` (25) is a plain
  `swtch()` that no signal disturbs.
- `copyin`/`copyout` (`sys/trans.c`) return **1** on a fault, 0 on
  success.
- `ioctl()` (`dev/tty.c`) handles `FIOCLEX` itself, for any descriptor,
  before calling the driver.
- `cdevsw[]` rows have **eight** fields: `d_open, d_close, d_read,
  d_write, d_ioctl, d_stop, d_addrs, d_ttys` (`h/conf.h`).
- `long` is 64 bits, and so is `tod_t` (`h/param.h`). The TOD clock's
  top bit has been set since 1971, so every current TOD value is
  negative as a signed `long`; the driver compares TOD values by
  difference, never directly.
- `times(2)` reports **microseconds** (`/usr/include/sys/times.h`), not
  ticks.

A nap tick is still 1/60 s (`NAPHZ` in `nap.h`), so programs written
for the V7 driver run unchanged; `naphz()` reports it.

## Files

| Source here | Goes to | Notes |
|---|---|---|
| `nap.c` | `/usr/src/sys/dev/nap.c` | the driver |
| `nap.h` | `/usr/src/sys/h/nap.h` **and** `/usr/include/nap.h` | kernel-header-free |
| `libnap.c` | wherever you build user programs | `nap()` / `naphz()` |
| `naptest.c` | same | verification |
| `nap.3` | `/usr/man/man3/nap.3` | |
| `install.sh` | run on UTS | edits the kernel sources and builds |
| `mkdeck.py` | run on your workstation | packs files for transfer to UTS |

`/usr/src` and `/usr` are different file systems on UTS (`dsk330` and
`dsk110`), so `/usr/include/nap.h` must be a copy, not a link.

## Steps

### 1. Get the files onto UTS

**Do not use `UTSPUN EXEC` for C source.** It punches in vmread's
cooked mode, which converts EBCDIC to ASCII with UTS's own table, and
that table disagrees with IND$FILE's for exactly the characters C needs.
Measured: `[` arrived as `0x17`, `]` as `|`, `^` as `~`, `|` as `0x00`,
and tabs were lost. Typing the files in at a UTS terminal is no better:
the tty driver's double-character substitution eats a literal `{`.

Instead, `mkdeck.py` packs files into a card deck in vmread's
**stream raw** mode, which copies bytes untouched and preserves exact
lengths:

```sh
python3 mkdeck.py guest nap.deck nap.c=nap/nap.c nap.h=nap/nap.h \
    libnap.c=nap/libnap.c naptest.c=nap/naptest.c nap.3=nap/nap.3 \
    install.sh=nap/install.sh
```

Destinations are relative to the user's home directory and must be
lower case; the directory (here `/home/guest/nap`) must already exist.
Upload `nap.deck` to CMS **in binary, as RECFM F LRECL 80** (with our
tn3270 client: `Transfer(Direction=send,Host=vm,HostFile="NAP DECK A",
LocalFile=nap.deck,Mode=binary,Recfm=fixed,Lrecl=80)`), then punch it
to the UTS virtual machine:

```
CP SPOOL PUNCH TO UTS
PUNCH NAP DECK A (NOHEADER
CP SPOOL PUNCH *
```

`/etc/vmread -c -h`, which UTS starts at boot, writes the files within
a few seconds.

### 2. Edit the kernel sources and build

As root, in the directory holding the files:

```sh
sh install.sh
```

It keeps each original as `FILE.nap-orig` and always edits from that
copy, so it is safe to run again. It installs `nap.c` and `nap.h`,
makes the three changes above, adds a row to the **end** of `cdevsw[]`
in `conf/conf.c`, adds `nap.o` to `dev/Makefile`, prints the major
number, runs `make uts` in `/usr/src/sys/conf`, and checks the new
kernel's symbol table for the nap entry points. Output is in
`build.log`.

**Append, do not insert.** Major numbers are positions in `cdevsw[]`,
and every `/dev` entry on the disk already encodes one. On the stock
UTS `conf.c` the last row is `sy` = 16, so nap is **17**; use the
number `install.sh` prints.

### 3. Install the kernel

The IPL program loads a kernel by file name from the root directory,
and `/uts`, `/unix`, `/AUTOUTS` and `/autouts` are four links to one
file. So copy over it in place, after saving the old one the way
`/etc/newsys` does:

```sh
cp /uts /outs
cp /usr/src/sys/conf/uts /uts
/etc/sync
```

### 4. Make the device node

```sh
/etc/mknod /dev/nap c 17 0
chmod 666 /dev/nap
```

### 5. Re-IPL

Shut UTS down with `/etc/shutdown 0`. It ends with `cpcmd logoff`, so
the virtual machine logs itself off. Start it again from a privileged
VM user (MAINT) as at any other time:

```
CP AUTOLOG UTS AMDAHL AUTOUTS
```

`/etc/rc` checks the kernel's name: `AUTOUTS`/`autouts` runs `dskfix`
and comes up multi-user unattended; any other name gives a single-user
shell on the console first.

**If shutdown hangs** after "System coming down immediately": that is
what happened here, with the cause undiagnosed. The DIALed terminal
that ran it stopped responding while its client was still connected,
so it was not a vanished client (dropping a client from a healthy UTS
is harmless; the next connection lands back on `Name:`). Reconnect to
the UTS virtual machine's console (`LOGON UTS`, password `AMDAHL`); `D PSW` showing
`031E0000 00000990` is UTS's enabled wait in `idle()`, so the kernel is
fine and only user space is stuck. `BEGIN`, wait for `/etc/update` to
have synced, then `LOGOFF` and AUTOLOG as above.

If the new kernel will not come up, the old one is in `/outs`. The IPL
program (on cylinder 0 of the 220 disk) asks for the name of the file
to load; log on to the `UTS` virtual machine, `IPL 220`, and answer
`outs`. (Not exercised: the new kernel came up first time.)

On a 3270 under UTS, **TESTREQ (SysReq) is end-of-file**, not a way out
of the DIAL (tube(4)); `exit` does not end a shell.

### 6. Verify

```sh
cc -o naptest naptest.c libnap.c
./naptest
```

Expect `got` within a tick or two of `want`, and `cpu` near zero for
the longer naps. `naptest` fails loudly on a nap that returns too fast
(no delay) or burns CPU proportional to its length (busy-waiting — the
thing this driver exists to avoid).

## Safety

**No interrupting a nap.** The nap sleeps at `PZERO`, so a signal
cannot take the process out of `sleep()` mid-nap and leave its slot
marked busy. The cost is that a nap cannot be interrupted, which is
why `NAPMAXT` caps it at two seconds.

**The comparator is shared with `tick()`.** A mistake in `napclock()`
or `clksckc()` stops the once-a-second tick, and with it alarms,
scheduling priorities and `iotic()`. Both functions are small for that
reason.

**`NNAP` (16) is only a table size.** V7's `timeout()` panics when its
20-entry callout table fills, which is why the V7 driver capped
concurrent naps at 4. UTS has no such table; a full nap table returns
`EAGAIN`.

## The UTS toolchain

Things that bit during the port, any of which will bite again:

- **`cc` cannot multiply two `long`s.** User code fails to compile
  ("No code table for op 133"); in the kernel the multiply compiles to
  a call of `_llmul`, which is not linked, and `ld` reports it
  undefined. Add, shift, compare and divide are fine.
- **`ld` writes its output file even when the link fails**, so "the
  kernel exists" proves nothing; check make's exit status and the build
  log for `Undefined`.
- `sh` has no functions, and `:` comment lines are commands, so an
  unquoted `;` in one runs whatever follows it.
- `grep -c` prefixes the count with the file name even for one file,
  and dies reading stdin; count with `grep ... | wc -l`.
- `awk` cannot assign `$0`, and rejects a bare `/re/` inside `&&`
  (write `$0 ~ /re/`).
- `chown` is `/etc/chown`; there is no `od` (use `xd`) and no `sum`.
- Typing at the shell over a 3270: a literal `[`, `]`, `^` or `\` does
  not arrive as itself (the double-character substitution of tty(3),
  and cent sign for backslash), and a command longer than the two-line
  input area is mangled. Put anything non-trivial in a file and send
  it with `mkdeck.py`.

## Status

**Tested on UTS under VM/370 Community Edition V1 R1.2, 2026-11-06.**
The kernel built by `install.sh` IPLs as `AUTOUTS`, the clock still
ticks (`date` is right; `sleep 5` takes 5 s), and `naptest` passes, as
root and as `guest`:

```
HZ = 60 (16 ms/tick)
ticks   want    got     cpu(us)
1       16ms    18ms    196
2       33ms    34ms    198
3       50ms    52ms    255
6       100ms   101ms   233
12      200ms   201ms   343
30      500ms   501ms   234
60      1000ms  1001ms  232
PASS
```

Six concurrent `naptest`s all passed with the same accuracy (worst
case 3 ms late on a one-second nap). CPU per nap is a constant ~0.2 ms
whatever its length, so it is sleeping, not spinning. VM/370's
simulation of the clock comparator is evidently good to a millisecond
or two on an idle system; how it degrades when VM is busy is
unmeasured.

`NNAP` 16 and `NAPMAXT` 120 are judgement calls, not measurements, and
the `EAGAIN` path (more than 16 concurrent nappers) has not been
exercised.
