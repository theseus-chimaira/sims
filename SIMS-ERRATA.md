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

The uploaded base already contains commit `d61c7bc` (`fix SLAVE bug`).  That
commit supplies the SLAVE device with its `DIB` through `DEVICE.context`, so
PDP-6 device 0020 can actually be mapped when SLAVE is enabled.

No additional SLAVE source change is required in this patch series.  The fix is
retained and is covered by DAIMOS device-probe testing.

## Validation policy

The simulator is built with `build_static.sh pdp6`, then the resulting static
`pdp6` is installed into the test tool prefix and used for DAIMOS PDP-6 device,
DCS socket, and magnetic-tape bootstrap tests.  The DAIMOS compatibility code
for the old DCS line bias/routing and the Type 516 parity workaround must be
removed when testing the corrected simulator.

## Static PDP-10 build: bundled SDL discovery

File: `build_static.sh`

The static-build script is intended to build the display-capable PDP-6/KA10
family against its own minimal static SDL2.  On a host without a system SDL2
development package, however, the script originally asked the SIMH makefile for
the target compiler command before the bundled SDL include/library paths were
visible to the makefile's feature detection.  The compiler command therefore
omitted `USE_DISPLAY` and the display sources entirely.  Rewriting
`sdl2-config` in that already-generated command could not restore code which
had never been selected.

The script now obtains the makefile's normal include/library search paths,
prepends the selected static SDL prefix, and supplies those paths plus the
selected `sdl2-config` while requesting each target command.  The resulting
fully static PDP-6 binary includes the headless DPY implementation even when
the host has no system SDL2 development package.

This was detected by the DAIMOS kernel device suite: the old static binary
reported `Non-existent device: DPY`; the corrected binary passes the DPY banner
test.

## Related seven-track media-generator defect

Repository: `pdp10-tools`, file: `mktap.c`

Once Type 516 READ parity checking was corrected and DAIMOS stopped suppressing
`PARITY_ERR`, the magnetic-tape bootstrap exposed a separate input-image defect:
`mktap` emitted only the six data bits of each seven-track tape character and
left bit 6 clear unconditionally.  A Type 516 seven-track image must carry the
per-character parity bit as well.

`mktap` now emits odd parity in bit `0100` for every six-bit character.  This is
not a SIMS workaround: it makes the generated tape image represent the medium
that the corrected Type 516 implementation reads.

## Validation results

Validation used the corrected DAIMOS tree with the old simulator compatibility
switches removed and Type 516 `PARITY_ERR` checking re-enabled.

- `gmake -j2 pdp6`: PASS, including the simulator register sanity check.
- `gmake -j2 pdp10-ka`: PASS, confirming the retained KA10/WAITS DCS branch
  still builds.
- `build_static.sh pdp6`: PASS; the result is an ELF x86-64 statically linked
  executable.
- DAIMOS PDP-6 DCS line-1 socket test: PASS.
- DAIMOS shared DCS/GE PI4 socket test: PASS.
- DAIMOS Type 516 MTC bootstrap with parity checking enabled: PASS.
- DAIMOS complete kernel conditional-MRES/device checkpoint suite: PASS,
  including the headless DPY banner test.

The kernel test environment also required the newer supplied DAS tree because
the older `local.tar.gz` assembler predates left-half relocation support.  No
DAIMOS source workaround was introduced for that stale assembler.
