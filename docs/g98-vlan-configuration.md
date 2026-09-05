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

These are `rc.conf` variables, not commands executed in sequence:

| Setting | Purpose |
|---|---|
| `yt921x_config_enable="YES"` | Enables the installed `yt921x_config` rc.d service. It runs after the driver is loaded and before FreeBSD configures network interfaces. |
| `yt921x_config_switches="..."` | Defines one policy per switch. The four VLAN IDs after each colon correspond, in order, to logical switch ports 0 through 3. The service enables 802.1Q mode, assigns each port its PVID and untagged VLAN membership, and adds logical CPU port 4 as a tagged member. |
| `ifconfig_eqos0="up"` and `ifconfig_eqos1="up"` | Bring up the two CPU-facing parent interfaces. They carry tagged frames between FreeBSD and the switches and normally do not need their own IP addresses. |
| `vlans_eqos0="..."` and `vlans_eqos1="..."` | Tell the standard FreeBSD network startup code which `vlan(4)` interfaces to create on each EQOS parent. |
| `create_args_vlanN="vlan N"` | Assign the actual IEEE 802.1Q VLAN ID to each named `vlan(4)` interface. The interface name alone does not define its VLAN ID. |
| `ifconfig_vlanN="..."` | Optionally assigns layer-3 addresses or other normal interface settings. OPNsense can manage this part instead. |

For example, an untagged frame entering G98 `lan8` is assigned PVID 101 by
YT9215S port 0. The switch sends it tagged through CPU port 4 and `eqos0`;
FreeBSD receives it on `vlan101`. Traffic transmitted through `vlan101`
follows the reverse path and leaves `lan8` untagged.

Keep three values consistent when changing a VLAN: its position in
`yt921x_config_switches`, its interface name in `vlans_eqosN`, and its
`create_args_vlanN` ID. Interface names are technically arbitrary, but using
the VLAN ID in the name avoids ambiguous configurations.

Distinct access VLANs provide the validated per-socket isolation. Ports placed
in the same VLAN can switch traffic directly; the driver does not currently
expose the YT9215S hardware port-isolation masks.

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
