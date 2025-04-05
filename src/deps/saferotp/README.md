
# SaferOTP library for RP2350

## Purpose

The one-time programmable fuses on the Raspberry Pi RP2350 chip
provides a great deal of flexibility.  The design has some nice
features, such as built-in multiple ways to store redundant data.
It also has much hidden complexity and edge cases.  This library
aims to provide a simpler, safer API surface to get and set data
that is stored in the OTP fuses.

## Problems

These can allow additional errors to sneak through.

* ECC algorithm in the datasheet was underspecified.
* Bootrom does not report errors when reading ECC data.
* Bootrom does not validate decoded ECC data (if detected
  a bitflip), if re-encoded with ECC, matches the read data.
* Memory-mapped OTP regions fail to provide any
  error reporting for invalidly-encoded / corrupt data.

Development for mere mortals can be expensive, as any
mistake may make the board unusable (one-time programmable).
Alternatively, development may become very slo, as each
write must be manually and carefully reviewed.

Having a set of well-tested higher-level APIs can greatly
reduce this burden.  Having the ability to easily virtualize
the OTP data is just amazing!

## Complexity

* There are many ways data could be encoded:
  * RAW ... 24 bits per row, without any redundancy or ECC
  * RBIT3 ... Storing the same 24 bits of data on three rows;
    Reads use per-bit majority voting to determine set bits
    (2 of 3 majority voting)
  * RBIT8 ... Storing the same 24 bits of data on eight rows;
    Reads use per-bit majority voting to determine set bits
    (3 of 8 majority voting)
  * BYTE3 ... Storing a the same 8 bits in a row three times;
    Reads use per-bit majority voting to determine set bits
    (2 of 3 majority voting)
  * ECC ... Storing 16 bits of usable data in a row, with
    the ability to correct any single-bit error, and detect
    any two-bit error via six ECC bits.  The final two bits
    (BRBP) allow a sector with any single bit flipped to still
    be usable to store an arbitrary value by indicating that
    all the 22 remaining bits should be inverted prior to
    performing error correction/detection.

Unfortunately, not only is it non-trivial to use for common tasks,
the Error Correction that the BIOS applies does not always do
what is expected.  For example, when an OTP row has ECC encoded
data, and multiple bits are flipped (e.g., forcing errors in the
encoded data), the BIOS may improperly return incorrect data.

In addition, when reading an OTP row with ECC encoded data, the
API appears to require reading **_TWO_** rows at a time.  The
datasheet says it will return 0xFFFFFFFF on a failure.
Since the API reads two rows at a time, it cannot indicate which
of the two rows had correct data (if any).  Moreover, it appears
that the bootrom might NOT report ECC errors ... perhaps
0xFFFFFFFF is only returned on permission errors?

As for the bit-by-bit voting ... That's template code that is
not glamorous, and requires great care to implement correctly.
Without simple, tested helper APIs, many projects may choose to
ignore the edge cases, or read only one copy ... effectively
losing all redundancy.

## API

Byte-count oriented for all encodings.  
* RAW reads and write use `uint32_t` for each row, of which
  only the least significant 24 bits contain data.
* Reading RBIT3 and RBIT8 are similar, but will read three
  (or eight) rows for each `uint32_t` and handle both errors
  and bit-by-bit majority voting.
* Writing RBIT3 and RBIT8 handle errors and edge cases,
  such as allowing the overall write to succeed, even when
  some bits across those rows are faulty (e.g., cannot be
  set to one but should be, or already set to 1 but need to
  store zero), so long as rows will decode correctly.
* Reads and writes of BYTE3 use `uint8_t` for each row,
  otherwise similarto RBIT3 / RBIT8
* Reads and writes of ECC data returns `uint16_t` for each row.


## Stretch Goals

* Virtualized OTP ... 
  * At any time, switch from interacting with the real OTP
    to interacting with a virtual copy.
  * By default, caches existing values from OTP.
  * Option to load from any other source (e.g., flash, ROM,
    config file, ...) a program desires.
  * Option to persist the current (real or) virtualized OTP
    to any other source (e.g., flash, config file, ...).
* OTP Directory Entries
  * Dynamically locate data stored in OTP
  * Add new entries to the directory
  * Increase yield for boards shipped with imperfect OTP ...
    fewer binned compared to using fixed rows
  * Directory entry type specifies how the data is encoded
    into the OTP rows (RAW, BYTE3, RBIT3, RBIT8, ECC, ...)
* Enforcing permissions for access to OTP registers.
  * ***Excluding*** OTP access keys (see below)
  * Unique permissions support for Secure-mode vs. Non-secure-mode
     * Initial implementation presumes all access is from secure mode
  * Application of OTP permissions in PAGEn_LOCK0 and PAGEn_LOCK1 OTP rows
  * Hard-coded write restriction for PAGE0 (per datasheet)
  * Reading soft-lock registers, at least at initialization
    * Detecting other writes would require use of the memory
      protection features of the RP2350 (to virtualize access
      to the soft-lock registers).
* Failing writes to special OTP rows with unsupported functionality
  * e.g., OTP access keys, bootloader keys, encryption keys, etc.

## Non-Goals

* Emulation of OTP Access Keys
  * Emulation of OTP access keys would require use of the memory
    protection features of the RP2350 (to virtualize access
    to the OTP Access Key registers).
  * OTP Access Key rows are currently treated the same
    as any other row.
* Having virtualized OTP have any effect on bootloader,
  boot encryption, etc.
* Other interactions with the CPU / hardware.
  * e.g., don't expect debug access to be locked out
    or require a key specified only in virtualized OTP.

## Debugging

This is a static library, and so gets embedded into other projects.
However, it has rich debug outputs through macros that can be
redefined as appropriate for your system... See:
* `saferotp_lib/saferotp_debug_stub.h`
* `saferotp_lib/saferotp_debug_stub.c`


This has been tested with input and output sent via Segger's RTT,
sent via TinyUSB serial port, and likely supports other debug output
modes, by simply defining a few macros at the head of the file.

## WORK IN PROGRESS

This library doesn't even have a version number yet.
However, given how many edge cases were uncovered during testing
of the RP2350 OTP implementation, it seemed this might be useful
to many other folks working with the RP2350 ... even if not
feature complete yet.




