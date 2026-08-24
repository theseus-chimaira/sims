# PDP-6/PDP-10 simulator errata

Audit date: 2026-08-24

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
