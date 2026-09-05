# G98 per-port VLAN configuration

The G98 connects two YT9215S switches to the RK3588 through `eqos0` and
`eqos1`. The driver exposes the switch ports through `etherswitch(4)`; it does
not create DSA-style `lan1` through `lan8` interfaces.

The supported design assigns one access VLAN to each physical RJ45 socket.
The external port is untagged, while logical CPU port 4 is a tagged member.
FreeBSD therefore sees each socket through a standard `vlan(4)` interface on
the corresponding EQOS parent.

## Persistent configuration

Add the following to `/etc/rc.conf`:

```sh
yt921x_config_enable="YES"
yt921x_config_switches="etherswitch0:101,102,103,104 etherswitch1:201,202,203,204"

ifconfig_eqos0="up"
ifconfig_eqos1="up"

vlans_eqos0="vlan101 vlan102 vlan103 vlan104"
vlans_eqos1="vlan201 vlan202 vlan203 vlan204"

create_args_vlan101="vlan 101"
create_args_vlan102="vlan 102"
create_args_vlan103="vlan 103"
create_args_vlan104="vlan 104"
create_args_vlan201="vlan 201"
create_args_vlan202="vlan 202"
create_args_vlan203="vlan 203"
create_args_vlan204="vlan 204"
```

Assign addresses with the normal FreeBSD or OPNsense network configuration.
For example:

```sh
ifconfig_vlan101="inet 192.168.101.1 netmask 255.255.255.0"
ifconfig_vlan201="inet 192.168.201.1 netmask 255.255.255.0"
```

Omit the `ifconfig_vlan*` address lines when OPNsense will assign the
interfaces and addresses itself.

## G98 socket map

Direct carrier tracing established this physical mapping:

| G98 socket | FreeBSD interface | Parent | Switch port | Access VLAN |
|---|---|---|---:|---:|
| `lan8` | `vlan101` | `eqos0` | 0 | 101 |
| `lan7` | `vlan102` | `eqos0` | 1 | 102 |
| `lan6` | `vlan103` | `eqos0` | 2 | 103 |
| `lan5` | `vlan104` | `eqos0` | 3 | 104 |
| `lan4` | `vlan201` | `eqos1` | 0 | 201 |
| `lan3` | `vlan202` | `eqos1` | 1 | 202 |
| `lan2` | `vlan203` | `eqos1` | 2 | 203 |
| `lan1` | `vlan204` | `eqos1` | 3 | 204 |

## Apply and verify

Validate the policy before changing the switch hardware:

```sh
service yt921x_config validate
```

Reboot to apply the switch policy before FreeBSD creates the VLAN interfaces.
Then verify the result:

```sh
ifconfig -g vlan
ifconfig vlan101
ifconfig vlan204
etherswitchcfg -f /dev/etherswitch0 info
etherswitchcfg -f /dev/etherswitch1 info
```

Each switch shares one 1 Gbps EQOS CPU link across its four host-facing VLAN
interfaces. Traffic switched directly between ports in the same hardware VLAN
does not consume that CPU link, but routed or firewall traffic does.
