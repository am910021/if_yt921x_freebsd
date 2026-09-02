#!/bin/sh
#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026 Fang, Wei-Xiang (Yuri) <am910021@gmail.com>

set -eu

dev=${1:-/dev/etherswitch0}
esc="etherswitchcfg -f $dev"

reg()
{
	$esc reg "$1" | awk -F= '{ print $2 }'
}

before_egr=$(reg 0x100084)
before_tpid=$(reg 0x100300)
before_stp=$(reg 0x18038c)
trap '$esc config vlan_mode none >/dev/null 2>&1 || true' EXIT INT TERM

$esc config vlan_mode dot1q >/dev/null
test $(( $(reg 0x100084) & 0x7000 )) -eq $(( 0x5000 ))
test $(( $(reg 0x100300) & 0xffff )) -eq $(( 0x8100 ))
test $(( $(reg 0x18038c) & 0xc000c )) -eq $(( 0xc000c ))

$esc config vlan_mode none >/dev/null
test $(( $(reg 0x100084) & 0x7000 )) -eq $(( before_egr & 0x7000 ))
test $(( $(reg 0x100300) & 0xffff )) -eq $(( before_tpid & 0xffff ))
test $(( $(reg 0x18038c) & 0xc000c )) -eq $(( before_stp & 0xc000c ))
trap - EXIT INT TERM

echo "$dev: VLAN policy enable/restore passed"
