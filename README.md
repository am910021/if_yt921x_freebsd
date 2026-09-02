# Motorcomm YT921x FreeBSD driver

Out-of-tree FreeBSD kernel module for Motorcomm YT921x Ethernet switches.

The current validation stage discovers `motorcomm,yt9215` children on an MDIO
bus, verifies the chip major and mode, and configures external port 9 as the
fixed 1 Gbps RGMII-TXID CPU link.  It deliberately does not reset the whole
switch or enable DSA tagging.

Each chip is also exposed through FreeBSD's native `etherswitch(4)` interface
as four user ports plus one fixed CPU port.  The driver reports real PVID and
ingress-filter state and exposes the hardware's VID-indexed groups 1 through
4094 (`vlangroupN` is VID N).  VLAN membership, tagged/untagged membership,
PVID, and tagged/untagged ingress rules are programmable; media changes are
not.

The CPU port is logical port 4.  It is automatically retained as a tagged
member whenever a VLAN has members, so normal FreeBSD `vlan(4)` traffic can
reach the switch without the proprietary YT921x DSA tag:

```sh
etherswitchcfg -f /dev/etherswitch0 vlangroup100 vlan 100 members 0,4t
etherswitchcfg -f /dev/etherswitch0 port0 pvid 100 ingress
```

The driver does not reset the switch or replace its power-on forwarding setup.
VLAN state is read directly from the hardware table, so populated groups remain
discoverable after a module reattach.  Remove a group with `vlan 0`.

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
