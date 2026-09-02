/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Fang, Wei-Xiang (Yuri) <am910021@gmail.com>
 */

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/socket.h>
#include <sys/systm.h>

#include <net/if.h>
#include <net/if_media.h>

#include <dev/etherswitch/etherswitch.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/openfirm.h>

#include "mdio_if.h"
#include "etherswitch_if.h"

#define YT921X_MDIO_ADDR	0x1d
#define YT921X_SWITCH_ID_MAX	3
#define YT921X_SMI_ADDR_READ(id)	(((id) << 2) | 0x1)
#define YT921X_SMI_DATA_READ(id)	(((id) << 2) | 0x3)
#define YT921X_SMI_ADDR_WRITE(id)	((id) << 2)
#define YT921X_SMI_DATA_WRITE(id)	(((id) << 2) | 0x2)

#define YT921X_CHIP_ID		0x80008
#define YT921X_EXT_CPU_PORT	0x8000c
#define YT921X_CHIP_MODE	0x80388
#define YT9215_CHIP_MAJOR	0x9002
#define YT9215S_CHIP_MODE	2

#define YT921X_SERDES_CTRL	0x80028
#define YT921X_PORT9_CTRL	0x80124
#define YT921X_PORT9_STATUS	0x80224
#define YT921X_MDIO_POLLING9	0x80368
#define YT921X_XMII_CTRL	0x80394
#define YT921X_XMII9		0x80408
#define YT921X_SERDES9		0x80090
#define YT921X_INT_MBUS_OP	0xf0000
#define YT921X_INT_MBUS_CTRL	0xf0004
#define YT921X_INT_MBUS_DOUT	0xf0008
#define YT921X_INT_MBUS_DIN	0xf000c
#define YT921X_FILTER_UNK_UCAST	0x180508
#define YT921X_FILTER_UNK_MCAST	0x18050c
#define YT921X_CPU_COPY		0x180690

#define YT921X_SERDES_CTRL_PORT9	(1U << 1)
#define YT921X_XMII_CTRL_PORT9	(1U << 0)
#define YT921X_XMII_MODE_MASK	(7U << 29)
#define YT921X_XMII_MODE_RGMII	(4U << 29)
#define YT921X_XMII_LINK	(1U << 19)
#define YT921X_XMII_ENABLE	(1U << 18)
#define YT921X_XMII_TX_DELAY_MASK	(0xfU << 13)
#define YT921X_XMII_TX_DELAY_2NS	(1U << 8)
#define YT921X_XMII_RX_DELAY_MASK	(0xfU << 3)

#define YT921X_PORT9_LINK_1G	0x000000fa
#define YT921X_SERDES9_LINK_1G	0x0000007a
#define YT921X_POLLING9_LINK_1G	0x0000001a
#define YT921X_EXT_CPU_PORT9	0x00004009
#define YT921X_CPU_COPY_TO_EXT	0x00000001
#define YT921X_FILTER_ALL_PORTS	0x000007ff

#define YT921X_MBUS_START	(1U << 0)
#define YT921X_MBUS_PORT_MASK	(0x1fU << 21)
#define YT921X_MBUS_PORT(port)	((uint32_t)(port) << 21)
#define YT921X_MBUS_REG_MASK	(0x1fU << 16)
#define YT921X_MBUS_REG(reg)	((uint32_t)(reg) << 16)
#define YT921X_MBUS_OP_MASK	(3U << 2)
#define YT921X_MBUS_WRITE	(1U << 2)
#define YT921X_MBUS_READ	(2U << 2)

struct yt921x_softc {
	device_t	dev;
	struct mtx	mtx;
	int		addr;
	int		switch_id;
	etherswitch_info_t info;
};

static phandle_t
yt921x_find_node(device_t dev)
{
	phandle_t child, mdio, node;

	node = ofw_bus_get_node(device_get_parent(device_get_parent(dev)));
	if (node <= 0)
		return (0);
	mdio = ofw_bus_find_child(node, "mdio");
	if (mdio <= 0)
		return (0);
	for (child = OF_child(mdio); child > 0; child = OF_peer(child)) {
		if (ofw_bus_node_is_compatible(child, "motorcomm,yt9215") &&
		    ofw_bus_node_status_okay(child))
			return (child);
	}
	return (0);
}

static int
yt921x_read_locked(struct yt921x_softc *sc, uint32_t reg, uint32_t *value)
{
	device_t mdio;
	int error, hi, lo;

	mdio = device_get_parent(sc->dev);
	error = MDIO_WRITEREG(mdio, sc->addr,
	    YT921X_SMI_ADDR_READ(sc->switch_id), reg >> 16);
	if (error == 0)
		error = MDIO_WRITEREG(mdio, sc->addr,
		    YT921X_SMI_ADDR_READ(sc->switch_id), reg & 0xffff);
	if (error == 0) {
		hi = MDIO_READREG(mdio, sc->addr,
		    YT921X_SMI_DATA_READ(sc->switch_id));
		lo = MDIO_READREG(mdio, sc->addr,
		    YT921X_SMI_DATA_READ(sc->switch_id));
		*value = ((uint32_t)(hi & 0xffff) << 16) | (lo & 0xffff);
	}
	return (error);
}

static int
yt921x_write_locked(struct yt921x_softc *sc, uint32_t reg, uint32_t value)
{
	device_t mdio;
	int error;

	mdio = device_get_parent(sc->dev);
	error = MDIO_WRITEREG(mdio, sc->addr,
	    YT921X_SMI_ADDR_WRITE(sc->switch_id), reg >> 16);
	if (error == 0)
		error = MDIO_WRITEREG(mdio, sc->addr,
		    YT921X_SMI_ADDR_WRITE(sc->switch_id), reg & 0xffff);
	if (error == 0)
		error = MDIO_WRITEREG(mdio, sc->addr,
		    YT921X_SMI_DATA_WRITE(sc->switch_id), value >> 16);
	if (error == 0)
		error = MDIO_WRITEREG(mdio, sc->addr,
		    YT921X_SMI_DATA_WRITE(sc->switch_id), value & 0xffff);
	return (error);
}

static int
yt921x_read(struct yt921x_softc *sc, uint32_t reg, uint32_t *value)
{
	int error;

	mtx_lock(&sc->mtx);
	error = yt921x_read_locked(sc, reg, value);
	mtx_unlock(&sc->mtx);
	return (error);
}

static int
yt921x_write(struct yt921x_softc *sc, uint32_t reg, uint32_t value)
{
	int error;

	mtx_lock(&sc->mtx);
	error = yt921x_write_locked(sc, reg, value);
	mtx_unlock(&sc->mtx);
	return (error);
}

static int
yt921x_update_locked(struct yt921x_softc *sc, uint32_t reg, uint32_t mask,
    uint32_t value)
{
	uint32_t old;
	int error;

	error = yt921x_read_locked(sc, reg, &old);
	if (error == 0 && (old & mask) != value)
		error = yt921x_write_locked(sc, reg, (old & ~mask) | value);
	return (error);
}

static int
yt921x_update(struct yt921x_softc *sc, uint32_t reg, uint32_t mask,
    uint32_t value)
{
	int error;

	mtx_lock(&sc->mtx);
	error = yt921x_update_locked(sc, reg, mask, value);
	mtx_unlock(&sc->mtx);
	return (error);
}

static int
yt921x_configure_port9(struct yt921x_softc *sc)
{
	uint32_t mask, value;
	int error;

	error = yt921x_write(sc, YT921X_EXT_CPU_PORT,
	    YT921X_EXT_CPU_PORT9);
	if (error != 0)
		return (error);
	error = yt921x_write(sc, YT921X_CPU_COPY, YT921X_CPU_COPY_TO_EXT);
	if (error != 0)
		return (error);
	error = yt921x_write(sc, YT921X_FILTER_UNK_UCAST,
	    YT921X_FILTER_ALL_PORTS);
	if (error != 0)
		return (error);
	error = yt921x_write(sc, YT921X_FILTER_UNK_MCAST,
	    YT921X_FILTER_ALL_PORTS);
	if (error != 0)
		return (error);
	error = yt921x_update(sc, YT921X_SERDES_CTRL,
	    YT921X_SERDES_CTRL_PORT9, 0);
	if (error != 0)
		return (error);
	error = yt921x_update(sc, YT921X_XMII_CTRL,
	    YT921X_XMII_CTRL_PORT9, YT921X_XMII_CTRL_PORT9);
	if (error != 0)
		return (error);
	mask = YT921X_XMII_MODE_MASK | YT921X_XMII_ENABLE |
	    YT921X_XMII_TX_DELAY_MASK | YT921X_XMII_TX_DELAY_2NS |
	    YT921X_XMII_RX_DELAY_MASK;
	value = YT921X_XMII_MODE_RGMII | YT921X_XMII_ENABLE |
	    YT921X_XMII_TX_DELAY_2NS;
	error = yt921x_update(sc, YT921X_XMII9, mask, value);
	if (error != 0)
		return (error);
	error = yt921x_write(sc, YT921X_PORT9_CTRL, YT921X_PORT9_LINK_1G);
	if (error != 0)
		return (error);
	error = yt921x_write(sc, YT921X_SERDES9, YT921X_SERDES9_LINK_1G);
	if (error != 0)
		return (error);
	error = yt921x_update(sc, YT921X_XMII9,
	    YT921X_XMII_LINK, YT921X_XMII_LINK);
	if (error != 0)
		return (error);
	return (yt921x_write(sc, YT921X_MDIO_POLLING9,
	    YT921X_POLLING9_LINK_1G));
}

static int
yt921x_log_port9(struct yt921x_softc *sc)
{
	static const uint32_t regs[] = {
		YT921X_SERDES_CTRL, YT921X_PORT9_CTRL, YT921X_PORT9_STATUS,
		YT921X_MDIO_POLLING9, YT921X_XMII_CTRL, YT921X_XMII9,
	};
	uint32_t values[nitems(regs)];
	int error;

	for (u_int i = 0; i < nitems(regs); i++) {
		error = yt921x_read(sc, regs[i], &values[i]);
		if (error != 0)
			return (error);
	}
	device_printf(sc->dev,
	    "port 9 registers: serdes=%#x ctrl=%#x status=%#x "
	    "poll=%#x xmii-ctrl=%#x xmii=%#x\n",
	    values[0], values[1], values[2], values[3], values[4], values[5]);
	return (0);
}

static void
yt921x_log_user_ports(struct yt921x_softc *sc)
{
	uint32_t status;
	int error;

	for (u_int port = 1; port <= 4; port++) {
		error = yt921x_read(sc, 0x80200 + 4 * port, &status);
		if (error == 0)
			device_printf(sc->dev, "port %u status=%#x%s\n", port,
			    status, (status & (1U << 9)) != 0 ? " link" : "");
	}
}

static int
yt921x_mbus_wait_locked(struct yt921x_softc *sc)
{
	uint32_t value;
	int error;

	for (u_int retry = 0; retry < 10000; retry++) {
		error = yt921x_read_locked(sc, YT921X_INT_MBUS_OP, &value);
		if (error != 0 || (value & YT921X_MBUS_START) == 0)
			return (error);
		DELAY(10);
	}
	return (ETIMEDOUT);
}

static int
yt921x_phy_read(struct yt921x_softc *sc, int port, int reg, uint16_t *value)
{
	uint32_t ctrl, data, mask;
	int error;

	mask = YT921X_MBUS_PORT_MASK | YT921X_MBUS_REG_MASK |
	    YT921X_MBUS_OP_MASK;
	ctrl = YT921X_MBUS_PORT(port) | YT921X_MBUS_REG(reg) |
	    YT921X_MBUS_READ;
	mtx_lock(&sc->mtx);
	error = yt921x_mbus_wait_locked(sc);
	if (error == 0)
		error = yt921x_update_locked(sc, YT921X_INT_MBUS_CTRL,
		    mask, ctrl);
	if (error == 0)
		error = yt921x_write_locked(sc, YT921X_INT_MBUS_OP,
		    YT921X_MBUS_START);
	if (error == 0)
		error = yt921x_mbus_wait_locked(sc);
	if (error == 0)
		error = yt921x_read_locked(sc, YT921X_INT_MBUS_DIN, &data);
	mtx_unlock(&sc->mtx);
	if (error == 0)
		*value = data;
	return (error);
}

static int
yt921x_phy_write(struct yt921x_softc *sc, int port, int reg, uint16_t value)
{
	uint32_t ctrl, mask;
	int error;

	mask = YT921X_MBUS_PORT_MASK | YT921X_MBUS_REG_MASK |
	    YT921X_MBUS_OP_MASK;
	ctrl = YT921X_MBUS_PORT(port) | YT921X_MBUS_REG(reg) |
	    YT921X_MBUS_WRITE;
	mtx_lock(&sc->mtx);
	error = yt921x_mbus_wait_locked(sc);
	if (error == 0)
		error = yt921x_update_locked(sc, YT921X_INT_MBUS_CTRL,
		    mask, ctrl);
	if (error == 0)
		error = yt921x_write_locked(sc, YT921X_INT_MBUS_DOUT, value);
	if (error == 0)
		error = yt921x_write_locked(sc, YT921X_INT_MBUS_OP,
		    YT921X_MBUS_START);
	if (error == 0)
		error = yt921x_mbus_wait_locked(sc);
	mtx_unlock(&sc->mtx);
	return (error);
}

static void
yt921x_log_phys(struct yt921x_softc *sc)
{
	uint16_t id1, id2;
	int error;

	for (u_int port = 1; port <= 4; port++) {
		error = yt921x_phy_read(sc, port, 2, &id1);
		if (error == 0)
			error = yt921x_phy_read(sc, port, 3, &id2);
		if (error == 0)
			device_printf(sc->dev, "port %u PHY ID %#04x:%#04x\n",
			    port, id1, id2);
		else
			device_printf(sc->dev, "cannot read port %u PHY: %d\n",
			    port, error);
	}
}

static etherswitch_info_t *
yt921x_getinfo(device_t dev)
{
	struct yt921x_softc *sc;

	sc = device_get_softc(dev);
	return (&sc->info);
}

static void
yt921x_lock(device_t dev)
{
	struct yt921x_softc *sc;

	sc = device_get_softc(dev);
	mtx_lock(&sc->mtx);
}

static void
yt921x_unlock(device_t dev)
{
	struct yt921x_softc *sc;

	sc = device_get_softc(dev);
	mtx_unlock(&sc->mtx);
}

static int
yt921x_readreg(device_t dev, int reg)
{
	struct yt921x_softc *sc;
	uint32_t value;

	sc = device_get_softc(dev);
	if (yt921x_read_locked(sc, reg, &value) != 0)
		return (0);
	return (value);
}

static int
yt921x_writereg(device_t dev, int reg, int value)
{
	struct yt921x_softc *sc;

	sc = device_get_softc(dev);
	return (yt921x_write_locked(sc, reg, value));
}

static int
yt921x_readphyreg(device_t dev, int phy, int reg)
{
	struct yt921x_softc *sc;
	uint16_t value;
	int error;

	if (phy < 1 || phy > 4 || reg < 0 || reg > 31)
		return (EINVAL);
	sc = device_get_softc(dev);
	error = yt921x_phy_read(sc, phy, reg, &value);
	return (error == 0 ? value : error);
}

static int
yt921x_writephyreg(device_t dev, int phy, int reg, int value)
{
	struct yt921x_softc *sc;

	if (phy < 1 || phy > 4 || reg < 0 || reg > 31)
		return (EINVAL);
	sc = device_get_softc(dev);
	return (yt921x_phy_write(sc, phy, reg, value));
}

static int
yt921x_getport(device_t dev, etherswitch_port_t *port)
{
	struct yt921x_softc *sc;
	struct ifmediareq *ifmr;
	uint32_t status;
	int hwport, subtype;

	if (port->es_port < 0 || port->es_port >= 5)
		return (EINVAL);
	sc = device_get_softc(dev);
	hwport = port->es_port == 4 ? 9 : port->es_port + 1;
	if (yt921x_read(sc, 0x80200 + 4 * hwport, &status) != 0)
		return (EIO);

	port->es_pvid = 0;
	port->es_flags = hwport == 9 ? ETHERSWITCH_PORT_CPU : 0;
	ifmr = &port->es_ifmr;
	ifmr->ifm_count = 0;
	ifmr->ifm_mask = 0;
	ifmr->ifm_current = IFM_ETHER |
	    (hwport == 9 ? IFM_1000_T : IFM_AUTO);
	switch (status & 0x7) {
	case 0:
		subtype = IFM_10_T;
		break;
	case 1:
		subtype = IFM_100_TX;
		break;
	default:
		subtype = IFM_1000_T;
		break;
	}
	ifmr->ifm_active = IFM_ETHER | subtype;
	if (status & (1U << 7))
		ifmr->ifm_active |= IFM_FDX;
	if (status & (1U << 6))
		ifmr->ifm_active |= IFM_ETH_RXPAUSE;
	if (status & (1U << 5))
		ifmr->ifm_active |= IFM_ETH_TXPAUSE;
	ifmr->ifm_status = IFM_AVALID;
	if (hwport == 9 || (status & (1U << 9)) != 0)
		ifmr->ifm_status |= IFM_ACTIVE;
	return (0);
}

static int
yt921x_setport(device_t dev, etherswitch_port_t *port)
{

	return (EOPNOTSUPP);
}

static int
yt921x_getvgroup(device_t dev, etherswitch_vlangroup_t *group)
{

	return (EOPNOTSUPP);
}

static int
yt921x_setvgroup(device_t dev, etherswitch_vlangroup_t *group)
{

	return (EOPNOTSUPP);
}

static void
yt921x_identify(driver_t *driver, device_t parent)
{

	if (device_find_child(parent, "yt921x", -1) == NULL)
		BUS_ADD_CHILD(parent, 0, "yt921x", -1);
}

static int
yt921x_probe(device_t dev)
{
	phandle_t node;

	node = yt921x_find_node(dev);
	if (node <= 0)
		return (ENXIO);
	device_set_desc(dev, "Motorcomm YT921x Ethernet switch");
	return (BUS_PROBE_DEFAULT);
}

static int
yt921x_attach(device_t dev)
{
	struct yt921x_softc *sc;
	phandle_t node;
	uint32_t chip_id, chip_mode, value;
	int error;

	sc = device_get_softc(dev);
	sc->dev = dev;
	sc->addr = YT921X_MDIO_ADDR;
	sc->switch_id = 0;
	sc->info.es_nports = 5;
	sc->info.es_nvlangroups = 0;
	strlcpy(sc->info.es_name, "Motorcomm YT9215S",
	    sizeof(sc->info.es_name));
	node = yt921x_find_node(dev);
	if (OF_getencprop(node, "reg", &value, sizeof(value)) > 0)
		sc->addr = value;
	if (OF_getencprop(node, "motorcomm,switch-id", &value,
	    sizeof(value)) > 0) {
		if (value > YT921X_SWITCH_ID_MAX)
			return (EINVAL);
		sc->switch_id = value;
	}

	mtx_init(&sc->mtx, device_get_nameunit(dev), NULL, MTX_DEF);
	chip_id = 0;
	error = yt921x_read(sc, YT921X_CHIP_ID, &chip_id);
	if (error != 0 || (chip_id >> 16) != YT9215_CHIP_MAJOR) {
		device_printf(dev, "unexpected chip ID %#010x (error %d)\n",
		    chip_id, error);
		mtx_destroy(&sc->mtx);
		return (ENXIO);
	}
	chip_mode = 0;
	error = yt921x_read(sc, YT921X_CHIP_MODE, &chip_mode);
	if (error != 0 || (chip_mode & 0x3) != YT9215S_CHIP_MODE) {
		device_printf(dev, "unexpected chip mode %#010x (error %d)\n",
		    chip_mode, error);
		mtx_destroy(&sc->mtx);
		return (ENXIO);
	}
	device_printf(dev,
	    "YT9215S at MDIO address %#x, chip ID %#010x, mode %#x\n",
	    sc->addr, chip_id, chip_mode);
	error = yt921x_log_port9(sc);
	if (error != 0)
		device_printf(dev, "cannot read port 9 registers: %d\n", error);
	error = yt921x_configure_port9(sc);
	if (error != 0) {
		device_printf(dev, "cannot configure port 9: %d\n", error);
		mtx_destroy(&sc->mtx);
		return (error);
	}
	device_printf(dev, "port 9 configured for RGMII-TXID at 1 Gbps\n");
	yt921x_log_port9(sc);
	yt921x_log_user_ports(sc);
	yt921x_log_phys(sc);
	bus_generic_probe(dev);
	return (bus_generic_attach(dev));
}

static int
yt921x_detach(device_t dev)
{
	struct yt921x_softc *sc;

	sc = device_get_softc(dev);
	bus_generic_detach(dev);
	mtx_destroy(&sc->mtx);
	return (0);
}

static device_method_t yt921x_methods[] = {
	DEVMETHOD(device_identify,	yt921x_identify),
	DEVMETHOD(device_probe,		yt921x_probe),
	DEVMETHOD(device_attach,	yt921x_attach),
	DEVMETHOD(device_detach,	yt921x_detach),
	DEVMETHOD(bus_add_child,	device_add_child_ordered),
	DEVMETHOD(etherswitch_getinfo,	yt921x_getinfo),
	DEVMETHOD(etherswitch_lock,	yt921x_lock),
	DEVMETHOD(etherswitch_unlock,	yt921x_unlock),
	DEVMETHOD(etherswitch_readreg,	yt921x_readreg),
	DEVMETHOD(etherswitch_writereg,	yt921x_writereg),
	DEVMETHOD(etherswitch_readphyreg,	yt921x_readphyreg),
	DEVMETHOD(etherswitch_writephyreg,	yt921x_writephyreg),
	DEVMETHOD(etherswitch_getport,	yt921x_getport),
	DEVMETHOD(etherswitch_setport,	yt921x_setport),
	DEVMETHOD(etherswitch_getvgroup,	yt921x_getvgroup),
	DEVMETHOD(etherswitch_setvgroup,	yt921x_setvgroup),
	DEVMETHOD_END
};

DEFINE_CLASS_0(yt921x, yt921x_driver, yt921x_methods,
    sizeof(struct yt921x_softc));
DRIVER_MODULE(yt921x, mdio, yt921x_driver, 0, 0);
DRIVER_MODULE(etherswitch, yt921x, etherswitch_driver, 0, 0);
MODULE_DEPEND(yt921x, mdio, 1, 1, 1);
MODULE_DEPEND(yt921x, etherswitch, 1, 1, 1);
MODULE_VERSION(yt921x, 1);
