# Motorcomm YT921x FreeBSD driver

Out-of-tree FreeBSD kernel module for Motorcomm YT921x Ethernet switches.

The current validation stage only discovers `motorcomm,yt9215` children on an
MDIO bus and verifies the chip major and mode.  It deliberately does not reset
or configure the switch yet.

The MDIO indirect-register protocol and register values were independently
implemented from the behavior documented by the Linux `yt921x` driver by David
Yang.  This implementation is BSD-2-Clause licensed and does not copy the GPL
driver implementation.

Build and load:

```sh
make -C src SYSDIR=/usr/src/sys
kldload ./src/yt921x.ko
```

Expected G98 result: two devices reporting YT9215 major `0x9002`, mode `2`, one
below each RK3588 EQOS MDIO bus.  The tested board reports chip ID
`0x90020001`.
