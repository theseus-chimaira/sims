# PDP-6/PDP-10 simulator errata

Audit date: 2026-08-24

Provenance notes updated: 2026-10-02

Base tree: `d61c7bce76c3374e253ff7953c613e8b7137af89`

This document records the PDP-6 simulator discrepancies that were found while
bringing up DAIMOS.  The fixes belong in the simulator: guest software should
not carry compatibility code for incorrect device semantics.

## Type 630 DCS

File: `PDP10/pdp6_dcs.c`

### PDP-6 line numbering

The PDP-6 Type 630 interface exposes scanner and send-buffer line numbers
without an offset.  The simulator had a `+2` result bias in `CONI DCSB` and a
matching `-2` bias in `CONO DCSB`.

The bias was introduced by SIMH commit `6c25bc60` as part of WAITS support.
The original Type 630 implementation in commit `930fc24f` did not contain the
bias.  The fix therefore applies direct line numbering to the PDP-6 build while
retaining the `+2/-2` convention for the KA10/WAITS build.

### DCSA/DCSB transmit selection

The PDP-6 Handbook describes the Type 630 transmit operations as follows:

- `CONO DCSB,E` loads the send-buffer line selection.
- `DATAO DCSA,E` sends through that send-buffer-selected line.
- `CONI DCSB,E` reads the receiver/scanner counter.
- `DATAO DCSB,E` sends through the receiver/scanner-selected line and releases
  the receiver scanner.

The simulator instead routed `DATAO DCSB` through the send buffer and routed
`DATAO DCSA` through the transmit scanner.  This can appear correct when only
line zero is tested because the initial scanner and send-buffer selections both
start at zero.

For the PDP-6 build the corrected implementation now:

- routes `DATAO DCSA` through `dcs_send_line`;
- routes `DATAO DCSB` through `dcs_rx_scan`;
- clears the selected receiver-ready flag after the transmit;
- releases the receiver scanner after `DATAO DCSB`;
- makes `CONI DCSB` return the receiver/scanner counter directly.

The KA10/WAITS path retains its historical SIMH behavior so the PDP-6 fix does
not regress WAITS compatibility.

## Type 516 magnetic tape, 7-track mode

File: `PDP10/pdp6_mtc.c`

### READ and READ BACKWARD parity verification

Seven-track tape characters contain six data bits and one parity bit.  The
simulator computed the expected parity bit correctly but compared it with zero
instead of comparing it with the parity bit actually stored in the tape byte.
This caused valid legacy seven-track images to assert `PARITY_ERR` for ordinary
characters whose expected parity bit was one, while failing to detect the
opposite mismatch.

The READ and READ BACKWARD paths now compare the computed parity bit with
`ch & 0x40`, matching the already-correct COMPARE parity check.

### WRITE and COMPARE six-bit word position

A 36-bit PDP-6 word contains six six-bit characters at bit positions 30, 24,
18, 12, 6, and 0.  READ and READ BACKWARD already used:

```
6 * (5 - i)
```

WRITE and COMPARE incorrectly used:

```
6 * (6 - i)
```

which starts at bit 36 and shifts every six-bit character one position too far.
WRITE and COMPARE now use the same `6 * (5 - i)` mapping as READ.

## SLAVE device mapping

File: `PDP10/pdp6_slave.c`

This commit supplies the SLAVE device with its `DIB` through `DEVICE.context`,
so PDP-6 device 0020 can actually be mapped when SLAVE is enabled.

## Type 167 I/O processor and Type 236 drum

File: `PDP10/pdp6_drum.c`

The Type 167/Type 236 implementation was reconstructed primarily from the
observable programming interface used by the PDP-6 JOSS-II sources rather
than from a complete hardware maintenance description. The source itself
records this provenance: the implemented programming interface follows the
JOSS-II sources, including their interpretation of the drum address as
16-word groups with unit selection in the upper address bits.

This is therefore a software-derived model of the hardware interface. It is
sufficient for the JOSS-derived behavior used during DAIMOS bring-up, but
details that are not visible to JOSS -- exact status-bit semantics, error
conditions, timing, rotational behavior, and other electrical/controller
details -- may be incomplete or inaccurate. The implementation should not be
treated as a verified reproduction of the Type 167 or Type 236 hardware
without comparison against original hardware documentation.

## Phil Petit calendar clock

File: `PDP10/ka10_pclk.c`

Stanford documentation establishes that Phil Petit's electronic calendar clock
was already attached to the Stanford system in March 1967, while that system
was still based on the PDP-6. SAILON-9, P. Petit, "Electronic Clock" (March
1967), describes an electronic clock providing microseconds, seconds, minutes,
hours, day, month, and year. The Stanford PDP-6 had entered service in June
1966; the KA10 PDP-10 was not installed until September 1968.

References:

- Stanford Artificial Intelligence Project, operating-note index, SAILON-9:
  https://ftpmirror.your.org/pub/misc/bitsavers/pdf/stanford/Stanford_CS_TR_Collection_2025-12-12/OCR/CS-TR-71-209-ocr.pdf
- SailDart SAIL history/timeline:
  https://www.saildart.org/simple/index-book-simple.html

The simulator did not implement the PDP-6 clock independently from original
PDP-6 schematics. This fork enabled and adapted the pre-existing SIMS
`ka10_pclk.c` Petit-clock model, originally written for the KA10/PDP-10
simulator. The historical record supports use of the Petit clock on the
PDP-6, but this audit has not established that the later KA10 interface and
the original PDP-6 installation were electrically or behaviorally identical.
The PDP-6 mode should therefore be regarded as historically motivated but not
hardware-verified.

## PDP-6 line printer

File: `PDP10/kx10_lp.c`

At the fork point the existing `kx10_lp.c` line-printer implementation was
already linked into the PDP-6 simulator and `lpt_dev` was already present in
the PDP-6 device table, but `NUM_DEVS_LP` explicitly disabled the device for
PDP-6 builds. Commit `31fcb5f` changed that configuration so the existing
line-printer device is instantiated for PDP-6.

No PDP-6-specific line-printer behavior was added or corrected as part of that
change. The device was enabled for practical use; its fidelity to the actual
PDP-6-connected line-printer hardware has not been established by this fork.
