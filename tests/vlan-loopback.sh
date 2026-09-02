#!/bin/sh
#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026 Fang, Wei-Xiang (Yuri) <am910021@gmail.com>

set -eu

if [ "$#" -lt 6 ] || [ "$#" -gt 7 ]; then
	echo "usage: $0 etherswitch parent outvid invid source target [port]" >&2
	exit 64
fi

dev=$1
parent=$2
outvid=$3
invid=$4
source=$5
target=$6
port=${7:-0}
[ "$port" -ge 0 ] 2>/dev/null && [ "$port" -le 3 ] 2>/dev/null || {
	echo "port must be between 0 and 3" >&2
	exit 64
}
hwport=$(( port + 1 ))
capture_timeout=${CAPTURE_TIMEOUT:-5}
esc="etherswitchcfg -f $dev"
iface=vlan$outvid
mode_enabled=0
parent_was_up=0

reg()
{
	$esc reg "$1" | awk -F= '{ print $2 }'
}

phyreg()
{
	$esc phy "$1" | awk -F= '{ print $2 }'
}

write_reg()
{
	$esc reg "$(printf '0x%x=0x%x' "$1" "$2")" >/dev/null
}

write_phy()
{
	$esc phy "$(printf '%s=0x%x' "$1" "$2")" >/dev/null
}

outlo=$(( 0x188000 + 8 * outvid ))
outhi=$(( outlo + 4 ))
inlo=$(( 0x188000 + 8 * invid ))
inhi=$(( inlo + 4 ))
old_outlo=$(reg "$outlo")
old_outhi=$(reg "$outhi")
old_inlo=$(reg "$inlo")
old_inhi=$(reg "$inhi")
pvid_reg=$(( 0x230010 + 4 * hwport ))
port_vlan_reg=$(( 0x230080 + 4 * hwport ))
ingress_tpid_reg=$(( 0x210010 + 4 * hwport ))
user_learn_reg=$(( 0x1803d0 + 4 * hwport ))
old_pvid=$(reg "$pvid_reg")
old_port_vlan=$(reg "$port_vlan_reg")
old_ingress_tpid=$(reg "$ingress_tpid_reg")
old_filter=$(reg 0x180280)
old_user_learn=$(reg "$user_learn_reg")
old_cpu_learn=$(reg 0x1803f4)
old_phy=$(phyreg "$hwport.0")

test $(( old_outlo & 0x10f00 )) -eq 0
test $(( old_outhi & 0x21e00 )) -eq 0
test $(( old_inlo & 0x10f00 )) -eq 0
test $(( old_inhi & 0x21e00 )) -eq 0
if ifconfig "$parent" | grep -q '<UP[,>]'; then
	parent_was_up=1
fi

cleanup()
{
	set +e
	ifconfig "$iface" destroy >/dev/null 2>&1
	write_phy "$hwport.0" "$old_phy"
	if [ "$mode_enabled" -eq 1 ]; then
		$esc vlangroup"$outvid" vlan 0 >/dev/null
		$esc vlangroup"$invid" vlan 0 >/dev/null
		write_reg "$pvid_reg" "$old_pvid"
		write_reg "$port_vlan_reg" "$old_port_vlan"
		write_reg "$ingress_tpid_reg" "$old_ingress_tpid"
		write_reg 0x180280 "$old_filter"
		write_reg "$user_learn_reg" "$old_user_learn"
		write_reg 0x1803f4 "$old_cpu_learn"
		$esc config vlan_mode none >/dev/null
	fi
	if [ "$parent_was_up" -eq 0 ]; then
		ifconfig "$parent" down >/dev/null 2>&1
	fi
}
trap cleanup EXIT INT TERM

$esc config vlan_mode dot1q >/dev/null
mode_enabled=1
$esc vlangroup"$outvid" vlan "$outvid" members "$port,4t" >/dev/null
$esc vlangroup"$invid" vlan "$invid" members "$port,4t" >/dev/null
$esc port"$port" pvid "$invid" ingress >/dev/null
write_reg "$user_learn_reg" $(( old_user_learn | 0x20000 ))
write_reg 0x1803f4 $(( old_cpu_learn | 0x20000 ))
write_phy "$hwport.0" 0x4140

ifconfig "$parent" up
ifconfig "$iface" create vlan "$outvid" vlandev "$parent"
ifconfig "$iface" ether "02:00:00:00:$(printf '%02x' $(( outvid / 256 ))):$(printf '%02x' $(( outvid % 256 )))"
ifconfig "$iface" inet "$source/24" up

arp -d "$target" >/dev/null 2>&1 || true
set +e
capture=$(
	timeout "$capture_timeout" tcpdump -ln -e -Q in -i "$parent" -c 1 \
	    "vlan $invid and arp" 2>&1 &
	tcpdump_pid=$!
	sleep 1
	ping -c 1 -W 1000 -S "$source" "$target" || true
	wait "$tcpdump_pid"
)
status=$?
set -e
printf '%s\n' "$capture"
test "$status" -eq 0
printf '%s\n' "$capture" | grep -q "vlan $invid"

$esc vlangroup"$invid" vlan "$invid" members 4t >/dev/null
arp -d "$target" >/dev/null 2>&1 || true
set +e
capture=$(
	timeout 3 tcpdump -ln -e -i "$parent" -Q in -c 1 \
	    "vlan $invid and arp" 2>&1 &
	tcpdump_pid=$!
	sleep 1
	ping -c 1 -W 1000 -S "$source" "$target" >/dev/null 2>&1 || true
	wait "$tcpdump_pid"
)
status=$?
set -e
test "$status" -eq 124
if printf '%s\n' "$capture" | grep -q "vlan $invid"; then
	exit 1
fi

cleanup
trap - EXIT INT TERM
test "$(reg "$outlo")" = "$old_outlo"
test "$(reg "$outhi")" = "$old_outhi"
test "$(reg "$inlo")" = "$old_inlo"
test "$(reg "$inhi")" = "$old_inhi"
test "$(reg "$pvid_reg")" = "$old_pvid"
test "$(reg "$port_vlan_reg")" = "$old_port_vlan"
test "$(reg "$ingress_tpid_reg")" = "$old_ingress_tpid"
test "$(reg 0x180280)" = "$old_filter"
test "$(reg "$user_learn_reg")" = "$old_user_learn"
test "$(reg 0x1803f4)" = "$old_cpu_learn"
test "$(phyreg "$hwport.0")" = "$old_phy"

echo "$dev/$parent port $port: VLAN loopback and restore passed"
