# Benchmarking on Raspberry Pi

_Hypothesis:_ Benchmarking on an array of Raspberry Pi's is "close enough" to
benchmarking on an array of real devices.

## Our Pi's

We have a number Raspberry Pi 3 Model B lying around.

Here is some information about their CPUs:

  model name  : ARMv7 Processor rev 4 (v7l)
  ...
  Features  : half thumb fastmult vfp edsp neon vfpv3 tls vfpv4 idiva idivt vfpd32 lpae evtstrm crc32
  ...
  Hardware  : BCM2709

## Cross-Compilation

_Hypothesis:_ Compiling core on a Raspberry Pi directly is too slow.

Cross-compiling a static version of core and copying it to the device is a
viable solution if this hypothesis is confirmed.
