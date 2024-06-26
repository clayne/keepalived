/*
 * Soft:        Keepalived is a failover program for the LVS project
 *              <www.linuxvirtualserver.org>. It monitor & manipulate
 *              a loadbalanced server pool using multi-layer checks.
 *
 * Part:        vrrp_iproute.c include file.
 *
 * Author:      Alexandre Cassen, <acassen@linux-vs.org>
 *
 *              This program is distributed in the hope that it will be useful,
 *              but WITHOUT ANY WARRANTY; without even the implied warranty of
 *              MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *              See the GNU General Public License for more details.
 *
 *              This program is free software; you can redistribute it and/or
 *              modify it under the terms of the GNU General Public License
 *              as published by the Free Software Foundation; either version
 *              2 of the License, or (at your option) any later version.
 *
 * Copyright (C) 2001-2017 Alexandre Cassen, <acassen@gmail.com>
 */

#ifndef _VRRP_IPROUTE_H
#define _VRRP_IPROUTE_H

/* global includes */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#if HAVE_DECL_RTA_ENCAP && HAVE_DECL_LWTUNNEL_ENCAP_MPLS
#include <linux/mpls.h>
#endif
#include <linux/rtnetlink.h>

/* local includes */
#include "list_head.h"
#include "vector.h"
#include "vrrp_ipaddress.h"
#include "vrrp_if.h"
#include "vrrp_static_track.h"

/* RTPROT_KEEPALIVED added in Linux 5.8 */
#ifndef RTPROT_KEEPALIVED
#define RTPROT_KEEPALIVED       18     /* Keepalived daemon */
#endif

/* Buffer sizes for printing */
#define	ROUTE_BUF_SIZE		1024

/* types definition */
#if HAVE_DECL_RTA_ENCAP	/* Since Linux 4.3 */
enum iproute_encap {
	IPROUTE_ENCAP_ID,
	IPROUTE_ENCAP_DSFIELD,
	IPROUTE_ENCAP_HOPLIMIT,
	IPROUTE_ENCAP_TTL = IPROUTE_ENCAP_HOPLIMIT,
	IPROUTE_ENCAP_FLAGS,
};
#define	IPROUTE_BIT_ENCAP_ID		(1<<IPROUTE_ENCAP_ID)
#define	IPROUTE_BIT_ENCAP_DSFIELD	(1<<IPROUTE_ENCAP_DSFIELD)
#define	IPROUTE_BIT_ENCAP_HOPLIMIT	(1<<IPROUTE_ENCAP_HOPLIMIT)
#define	IPROUTE_BIT_ENCAP_TTL		(1<<IPROUTE_ENCAP_TTL)
#define	IPROUTE_BIT_ENCAP_FLAGS		(1<<IPROUTE_ENCAP_FLAGS)

#if HAVE_DECL_LWTUNNEL_ENCAP_MPLS
#define MAX_MPLS_LABELS	2
typedef struct mpls_label mpls_labels[MAX_MPLS_LABELS];

typedef struct _encap_mpls {
	mpls_labels	addr;
	size_t		num_labels;
} encap_mpls_t;
#endif

typedef struct _encap_ip {
	uint64_t	id;
	ip_address_t	*dst;
	ip_address_t	*src;
	uint8_t		tos;
	uint16_t	flags;
	uint8_t		ttl;
} encap_ip_t;

#if HAVE_DECL_LWTUNNEL_ENCAP_ILA
typedef struct _encap_ila {
	uint64_t	locator;
} encap_ila_t;
#endif

typedef struct _encap_ip6 {
	uint64_t	id;
	ip_address_t	*dst;
	ip_address_t	*src;
	uint8_t		tc;
	uint16_t	flags;
	uint8_t		hoplimit;
} encap_ip6_t;

typedef struct _encap {
	uint16_t	type;
	uint32_t	flags;
	union {
#if HAVE_DECL_LWTUNNEL_ENCAP_MPLS
		encap_mpls_t	mpls;
#endif
		encap_ip_t	ip;
#if HAVE_DECL_LWTUNNEL_ENCAP_ILA
		encap_ila_t	ila;
#endif
		encap_ip6_t	ip6;
	};
} encap_t;
#endif

typedef struct _nexthop {
	uint32_t	mask;
	ip_address_t	*addr;
	interface_t	*ifp;
	uint8_t		weight;
	uint8_t		flags;
	uint32_t	realms;
#if HAVE_DECL_RTA_ENCAP
	encap_t		encap;
#endif
//#if HAVE_DECL_RTA_NEWDST
//	ip_address_t *as_to;
//#endif
	/* linked list member */
	list_head_t	e_list;
} nexthop_t;

enum ip_route {
	IPROUTE_DSFIELD = 0,
	IPROUTE_TYPE,
	IPROUTE_PROTOCOL,
	IPROUTE_SCOPE,
	IPROUTE_METRIC,
	IPROUTE_WEIGHT,
	IPROUTE_EXPIRES,
	IPROUTE_MTU,
	IPROUTE_HOPLIMIT,
	IPROUTE_ADVMSS,
	IPROUTE_RTT,
	IPROUTE_RTTVAR,
	IPROUTE_REORDERING,
	IPROUTE_WINDOW,
	IPROUTE_CWND,
	IPROUTE_SSTHRESH,
	IPROUTE_RTO_MIN,
	IPROUTE_INITCWND,
	IPROUTE_INITRWND,
	IPROUTE_QUICKACK,
	IPROUTE_PREF,
	IPROUTE_FASTOPEN_NO_COOKIE,
	IPROUTE_TTL_PROPAGATE,
	IPROUTE_ADD_ROUTE,
	IPROUTE_APPEND_ROUTE
};

#define	IPROUTE_BIT_DSFIELD	(1<<IPROUTE_DSFIELD)
#define	IPROUTE_BIT_TYPE	(1<<IPROUTE_TYPE)
#define	IPROUTE_BIT_PROTOCOL	(1<<IPROUTE_PROTOCOL)
#define	IPROUTE_BIT_SCOPE	(1<<IPROUTE_SCOPE)
#define	IPROUTE_BIT_METRIC	(1<<IPROUTE_METRIC)
#define	IPROUTE_BIT_WEIGHT	(1<<IPROUTE_WEIGHT)
#define	IPROUTE_BIT_EXPIRES	(1<<IPROUTE_EXPIRES)
#define	IPROUTE_BIT_MTU		(1<<IPROUTE_MTU)
#define	IPROUTE_BIT_HOPLIMIT	(1<<IPROUTE_HOPLIMIT)
#define	IPROUTE_BIT_ADVMSS	(1<<IPROUTE_ADVMSS)
#define	IPROUTE_BIT_RTT		(1<<IPROUTE_RTT)
#define	IPROUTE_BIT_RTTVAR	(1<<IPROUTE_RTTVAR)
#define	IPROUTE_BIT_REORDERING	(1<<IPROUTE_REORDERING)
#define	IPROUTE_BIT_WINDOW	(1<<IPROUTE_WINDOW)
#define	IPROUTE_BIT_CWND	(1<<IPROUTE_CWND)
#define	IPROUTE_BIT_SSTHRESH	(1<<IPROUTE_SSTHRESH)
#define	IPROUTE_BIT_RTO_MIN	(1<<IPROUTE_RTO_MIN)
#define	IPROUTE_BIT_INITCWND	(1<<IPROUTE_INITCWND)
#define	IPROUTE_BIT_INITRWND	(1<<IPROUTE_INITRWND)
#define	IPROUTE_BIT_QUICKACK	(1<<IPROUTE_QUICKACK)
#define	IPROUTE_BIT_PREF	(1<<IPROUTE_PREF)
#define	IPROUTE_BIT_FASTOPEN_NO_COOKIE	(1<<IPROUTE_FASTOPEN_NO_COOKIE)
#define	IPROUTE_BIT_TTL_PROPAGATE (1<<IPROUTE_TTL_PROPAGATE)
#define	IPROUTE_BIT_ADD		(1<<IPROUTE_ADD_ROUTE)
#define	IPROUTE_BIT_APPEND	(1<<IPROUTE_APPEND_ROUTE)

typedef struct _ip_route {
	ip_address_t		*dst;
	ip_address_t		*src;
	ip_address_t		*pref_src;
	uint8_t			family;
	uint8_t			tos;
	uint32_t		table;
	uint8_t			protocol;
	uint8_t			scope;
	uint32_t		metric;
	ip_address_t		*via;
	interface_t		*oif;
	uint32_t		flags;
	uint32_t		features;
#if HAVE_DECL_RTAX_QUICKACK
	bool			quickack;
#endif
#if HAVE_DECL_RTA_EXPIRES
	uint32_t		expires;
#endif
	uint32_t		lock;
	uint32_t		mtu;
	uint8_t			hoplimit;
	uint32_t		advmss;
//#if HAVE_DECL_RTA_NEWDST
//	ip_address_t		*as_to;
//#endif
	uint32_t		rtt;
	uint32_t		rttvar;
	uint32_t		reordering;
	uint32_t		window;
	uint32_t		cwnd;
	uint32_t		ssthresh;
	uint32_t		rto_min;
	uint32_t		initcwnd;
	uint32_t		initrwnd;
#if HAVE_DECL_RTAX_CC_ALGO
	char			*congctl;
#endif
#if HAVE_DECL_RTA_PREF
	uint8_t			pref;
#endif
#if HAVE_DECL_RTAX_FASTOPEN_NO_COOKIE
	bool			fastopen_no_cookie;
#endif
#if HAVE_DECL_RTA_TTL_PROPAGATE
	bool			ttl_propagate;
#endif
	uint8_t			type;

	uint32_t		realms;
#if HAVE_DECL_RTA_ENCAP
	encap_t			encap;
#endif
	list_head_t		nhs;		/* nexthop_t */
	uint32_t		mask;
	bool			dont_track;	/* used for virtual routes */
	static_track_group_t	*track_group;	/* used for static routes */
	bool			set;
	uint32_t		configured_ifindex;	/* Index of interface route is configured on */

	/* linked list member */
	list_head_t		e_list;
} ip_route_t;

#define IPROUTE_DEL	0
#define IPROUTE_ADD	1
#define IPROUTE_REPLACE	2

/* prototypes */
extern unsigned short add_addr2req(struct nlmsghdr *, size_t, unsigned short, ip_address_t *);
extern bool netlink_rtlist(list_head_t *, int, bool);
extern void free_iproute(ip_route_t *);
extern void free_iproute_list(list_head_t *);
extern void format_iproute(const ip_route_t *, char *, size_t);
extern void dump_iproute(FILE *, const ip_route_t *);
extern void dump_iproute_list(FILE *, const list_head_t *);
extern void alloc_route(list_head_t *, const vector_t *, bool);
extern void clear_diff_routes(list_head_t *, list_head_t *);
extern void clear_diff_static_routes(void);
extern void reinstate_static_route(ip_route_t *);

#endif
