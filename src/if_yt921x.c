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
#include <sys/systm.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/openfirm.h>

#include "mdio_if.h"

#define YT921X_MDIO_ADDR	0x1d
#define YT921X_SWITCH_ID_MAX	3
#define YT921X_SMI_ADDR_READ(id)	(((id) << 2) | 0x1)
#define YT921X_SMI_DATA_READ(id)	(((id) << 2) | 0x3)

#define YT921X_CHIP_ID		0x80008
#define YT921X_CHIP_MODE	0x80388
#define YT9215_CHIP_MAJOR	0x9002
#define YT9215S_CHIP_MODE	2

struct yt921x_softc {
	device_t	dev;
	struct mtx	mtx;
	int		addr;
	int		switch_id;
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
yt921x_read(struct yt921x_softc *sc, uint32_t reg, uint32_t *value)
{
	device_t mdio;
	int error, hi, lo;

	mdio = device_get_parent(sc->dev);
	mtx_lock(&sc->mtx);
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
	mtx_unlock(&sc->mtx);
	return (error);
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
	return (0);
}

static int
yt921x_detach(device_t dev)
{
	struct yt921x_softc *sc;

	sc = device_get_softc(dev);
	mtx_destroy(&sc->mtx);
	return (0);
}

static device_method_t yt921x_methods[] = {
	DEVMETHOD(device_identify,	yt921x_identify),
	DEVMETHOD(device_probe,		yt921x_probe),
	DEVMETHOD(device_attach,	yt921x_attach),
	DEVMETHOD(device_detach,	yt921x_detach),
	DEVMETHOD_END
};

DEFINE_CLASS_0(yt921x, yt921x_driver, yt921x_methods,
    sizeof(struct yt921x_softc));
DRIVER_MODULE(yt921x, mdio, yt921x_driver, 0, 0);
MODULE_DEPEND(yt921x, mdio, 1, 1, 1);
MODULE_VERSION(yt921x, 1);
