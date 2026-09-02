# Motorcomm YT921x FreeBSD driver

Out-of-tree FreeBSD kernel module for Motorcomm YT921x Ethernet switches.

The driver discovers `motorcomm,yt9215` children on an MDIO bus, verifies the
chip major and mode, resets the switch, and configures external port 9 as the
fixed 1 Gbps RGMII-TXID CPU link.  It deliberately does not enable DSA tagging.

Each chip is also exposed through FreeBSD's native `etherswitch(4)` interface
as four user ports plus one fixed CPU port.  The driver reports real PVID and
ingress-filter state and exposes the hardware's VID-indexed groups 1 through
4094 (`vlangroupN` is VID N).  VLAN membership, tagged/untagged membership,
PVID, and tagged/untagged ingress rules are programmable; media changes are
not.

VLAN mode is disabled on attach.  Enable standard 802.1Q mode before
configuring a VLAN; this selects entry-based egress tagging, TPID `0x8100`,
and forwarding state for the five exposed ports.  Setting mode to `none`, or
unloading the module, restores the fields that were present before enablement.

The CPU port is logical port 4.  It is automatically retained as a tagged
member whenever a VLAN has members, so normal FreeBSD `vlan(4)` traffic can
reach the switch without the proprietary YT921x DSA tag:

```sh
etherswitchcfg -f /dev/etherswitch0 config vlan_mode dot1q
etherswitchcfg -f /dev/etherswitch0 vlangroup100 vlan 100 members 0,4t
etherswitchcfg -f /dev/etherswitch0 port0 pvid 100 ingress
etherswitchcfg -f /dev/etherswitch0 config vlan_mode none
```

Attach resets the switch and therefore clears firmware or stale VLAN state.
While loaded, VLAN state is read directly from the hardware table.  Remove a
group with `vlan 0`.

Do not reload the module at runtime: the switch reset interrupts the parent
EQOS RGMII clock.  Reboot after unloading it.

The MDIO indirect-register protocol and register values were independently
implemented from the behavior documented by the Linux `yt921x` driver by David
Yang.  Entry-based egress-mode behavior was checked against Motorcomm's public
YT9215 SDK headers and independently validated on G98 hardware.  This
implementation is BSD-2-Clause licensed and copies code from neither source.

Build and load:

```sh
make -C src SYSDIR=/usr/src/sys
kldload ./src/yt921x.ko
```

Expected G98 result: two devices reporting YT9215 major `0x9002`, mode `2`, one
below each RK3588 EQOS MDIO bus.  The tested board reports chip ID
`0x90020001`.
