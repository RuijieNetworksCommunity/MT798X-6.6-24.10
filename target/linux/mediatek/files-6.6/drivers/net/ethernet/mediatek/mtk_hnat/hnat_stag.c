/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (c) 2020 MediaTek Inc.
 * Author: Landen Chao <landen.chao@mediatek.com>
 */
#include <linux/of_device.h>
#include <net/netfilter/nf_flow_table.h>
#include <net/dsa.h>
#include <linux/dsa/8021q.h>
#include "hnat.h"

int hnat_get_dsa_port(struct net_device **dev, u16 *push_vid)
{
#if IS_ENABLED(CONFIG_NET_DSA)
	struct dsa_port *dp;

	dp = dsa_port_from_netdev(*dev);
	if (IS_ERR(dp))
		return -ENODEV;

	switch (dp->cpu_dp->tag_ops->proto) {
	case DSA_TAG_PROTO_MTK:
		if (push_vid)
			*push_vid = 0;
		break;
	case DSA_TAG_PROTO_MXL862_8021Q:
		if (push_vid)
			*push_vid = dsa_tag_8021q_standalone_vid(dp);
		break;
	default:
		return -ENODEV;
	}

	*dev = dsa_port_to_master(dp);

	return dp->index;
#else
	return -ENODEV;
#endif
}

int hnat_dsa_fill_stag(const struct net_device *netdev,
		       struct foe_entry *entry,
		       struct flow_offload_hw_path *hw_path,
		       u16 eth_proto,
		       int mape)
{
#if defined(CONFIG_NET_DSA)

	int ret = 0;
	struct net_device *ndev, *mdev;
	if (hw_path->flags & BIT(DEV_PATH_VLAN))
		ndev = hw_path->dev;
	else
		ndev = (struct net_device *)netdev;
	mdev = ndev;

	int dsa_port;
	u16 push_vid;
	dsa_port = hnat_get_dsa_port(&mdev, &push_vid);

	/* In the case MAPE LAN --> WAN, binding entry is to CPU.
	 * Do not add special tag.
	 */
	if (IS_WAN(ndev) && mape)
		return dsa_port;

	if (dsa_port >= 0) {
		if (push_vid)
			ret = hnat_foe_entry_set_vlan(entry, push_vid);
		else
			ret = hnat_foe_entry_set_dsa(entry, dsa_port);
	}

	if (unlikely(ret < 0))
		return ret;

	// struct mtk_foe_mac_info *l2 =  hnat_foe_entry_l2(entry);
	// printk_ratelimited("dsa_port: %d, push_vid: %d vlan1:%d vlan2: %d, vpm: %d",
	// 			dsa_port, push_vid,
	// 			l2->vlan1,
	// 			l2->vlan2,
	// 			entry->bfib1.vpm);

	return dsa_port;
#else
	return -EINVAL;
#endif
}
