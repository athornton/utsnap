#!/usr/bin/env python3
"""mkdeck -- pack files into one card deck for UTS's vmread, untranslated.

    mkdeck.py USER OUTDECK FILE[=DEST] ...

Writes OUTDECK as a sequence of 80-byte card images.  Upload it to CMS
in BINARY as RECFM F LRECL 80, then punch it to the UTS virtual machine:

    CP SPOOL PUNCH TO UTS
    PUNCH NAP DECK A (NOHEADER
    CP SPOOL PUNCH *

vmread(3) on UTS turns each file in the deck into a file owned by USER,
named DEST (default: FILE's basename) relative to USER's home directory
unless DEST is absolute.

Why not UTSPUN EXEC: it punches in vmread's cooked ("c") mode, which
translates EBCDIC to ASCII through UTS's own table, and that table does
not agree with what IND$FILE's ASCII mode produced for C source.
Measured: '[' arrived as 0x17, ']' as '|', '^' as '~', '|' as 0x00, and
tabs were lost.  So this deck uses stream raw ("sr") mode instead, in
which vmread copies bytes untouched and the 2-byte lengths give the
exact file size:

    card 1:  0x00000107 (MAGIC1), then "QSR USER DEST" in EBCDIC,
             blank-padded to 80 bytes (vmread: HDRSIZ = CARDSIZ - 4)
    then:    <2-byte length><data> ... <0x0000>, zero-padded to a card

and vmread skips to the next card boundary before reading the next
header, so any number of files can share one deck (readin() in
/usr/src/etc/vmread.c).

The header must be upper-case EBCDIC and vmread lowercases its letters,
so DEST must be lower case.  (vmread keeps the case of a character that
follows a cent sign, but the EBCDIC-to-ASCII table that decides what a
cent sign is has not been checked, so this script does not try.)
"""

import os
import sys

CARD = 80
MAGIC1 = b"\x00\x00\x01\x07"
CHUNK = 32000           # vmread reads the length as a signed short


def ebcdic_header(text):
    return text.upper().encode("cp037")


def pad(data, fill):
    rem = len(data) % CARD
    return data if rem == 0 else data + fill * (CARD - rem)


def file_cards(user, dest, data):
    head = MAGIC1 + ebcdic_header("QSR %s %s" % (user, dest))
    if len(head) > CARD:
        sys.exit("mkdeck: header too long for one card: %s" % dest)
    head = pad(head, b"\x40")
    body = b""
    for i in range(0, len(data), CHUNK):
        piece = data[i:i + CHUNK]
        body += len(piece).to_bytes(2, "big") + piece
    body += b"\x00\x00"
    return head + pad(body, b"\x00")


def main(argv):
    if len(argv) < 4:
        sys.exit(__doc__.split("\n\n")[1])
    user, outdeck = argv[1], argv[2]
    deck = b""
    for spec in argv[3:]:
        src, _, dest = spec.partition("=")
        dest = dest or os.path.basename(src)
        if dest != dest.lower():
            sys.exit("mkdeck: DEST must be lower case: %s" % dest)
        with open(src, "rb") as f:
            data = f.read()
        if b"\r" in data:
            sys.exit("mkdeck: %s contains CR; UTS wants bare LF" % src)
        deck += file_cards(user.upper(), dest, data)
    with open(outdeck, "wb") as f:
        f.write(deck)
    print("%s: %d cards" % (outdeck, len(deck) // CARD))


if __name__ == "__main__":
    main(sys.argv)
