/*
 * Soft:        Keepalived is a failover program for the LVS project
 *              <www.linuxvirtualserver.org>. It monitor & manipulate
 *              a loadbalanced server pool using multi-layer checks.
 *
 * Part:        Dynamic data structure definition
 *
 * Author:      Ilya Voronin, <ivoronin@gmail.com>
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
 * Copyright (C) 2015-2017 Alexandre Cassen, <acassen@gmail.com>
 */

#include "config.h"

#include "bfd.h"
#include "global_data.h"
#include "bfd_data.h"
#include "logger.h"
#include "parser.h"
#include "memory.h"
#include "utils.h"
#include "main.h"
#include "assert_debug.h"

/* Global vars */
bfd_data_t *bfd_data;
bfd_data_t *old_bfd_data;
char *bfd_buffer;


/*
 *	bfd_t functions
 */
/* Initialize bfd_t */
bfd_t *
alloc_bfd(const char *name)
{
	bfd_t *bfd;

	assert(name);

	if (strlen(name) >= sizeof(bfd->iname)) {
		report_config_error(CONFIG_GENERAL_ERROR, "Configuration error: BFD instance %s"
			    " name too long (maximum length is %zu"
			    " characters) - ignoring", name,
			    sizeof(bfd->iname) - 1);
		return NULL;
	}

	if (find_bfd_by_name(name)) {
		report_config_error(CONFIG_GENERAL_ERROR,
			    "Configuration error: BFD instance %s"
			    " already configured - ignoring", name);
		return NULL;
	}

	PMALLOC(bfd);
	INIT_LIST_HEAD(&bfd->e_list);
	strcpy(bfd->iname, name);

	/* Set defaults */
	bfd->local_min_rx_intv = BFD_MINRX_DEFAULT * TIMER_HZ / 1000;
	bfd->local_min_tx_intv = BFD_MINTX_DEFAULT * TIMER_HZ / 1000;
	bfd->local_idle_tx_intv = BFD_IDLETX_DEFAULT * TIMER_HZ / 1000;
	bfd->local_detect_mult = BFD_MULTIPLIER_DEFAULT;

	bfd->ttl = 0;
	bfd->max_hops = 0;

	/* Initialize internal variables */
	bfd->fd_out = -1;
	bfd->thread_open_fd_out = NULL;
	bfd->thread_out = NULL;
	bfd->thread_exp = NULL;
	bfd->thread_rst = NULL;
	bfd->sands_out = TIMER_NEVER;
	bfd->sands_exp = TIMER_NEVER;
	bfd->sands_rst = TIMER_NEVER;

	return bfd;
}

void
free_bfd(bfd_t *bfd)
{
	list_del_init(&bfd->e_list);
	FREE(bfd);

}
static void
free_bfd_list(list_head_t *l)
{
	bfd_t *bfd, *bfd_tmp;

	list_for_each_entry_safe(bfd, bfd_tmp, l, e_list)
		free_bfd(bfd);
}

static void
conf_write_sands(FILE *fp, const char *text, unsigned long sands)
{
	char time_str[26];
	time_t secs;

	if (sands == TIMER_NEVER) {
		conf_write(fp, "   %s = [disabled]", text);
		return;
	}

	secs = sands / TIMER_HZ;
	if (!ctime_r(&secs, time_str))
		strcpy(time_str, "invalid time ");
	conf_write(fp, "   %s = %" PRI_time_t ".%6.6lu (%.19s.%6.6lu)", text, secs, sands % TIMER_HZ, time_str, sands % TIMER_HZ);
}

/* Dump BFD instance configuration parameters */
static void
dump_bfd(FILE *fp, const bfd_t *bfd)
{
	char time_str[26];

	conf_write(fp, " BFD Instance = %s", bfd->iname);
	conf_write(fp, "   Neighbor IP = %s",
		    inet_sockaddrtos(&bfd->nbr_addr));

	if (bfd->src_addr.ss_family != AF_UNSPEC)
		conf_write(fp, "   Source IP = %s",
			    inet_sockaddrtos(&bfd->src_addr));

	conf_write(fp, "   Required min RX interval = %u us", bfd->local_min_rx_intv);
	conf_write(fp, "   Desired min TX interval = %u us", bfd->local_min_tx_intv);
	conf_write(fp, "   Desired idle TX interval = %u us", bfd->local_idle_tx_intv);
	conf_write(fp, "   Detection multiplier = %d", bfd->local_detect_mult);
	conf_write(fp, "   %s = %d",
		    bfd->nbr_addr.ss_family == AF_INET ? "TTL" : "hoplimit",
		    bfd->ttl);
	conf_write(fp, "   max_hops = %d",
		    bfd->max_hops);
	conf_write(fp, "   passive = %s", bfd->passive ? "true" : "false");
#ifdef _WITH_VRRP_
	conf_write(fp, "   send event to VRRP process = %s",
		    bfd->vrrp ? "Yes" : "No");
#endif
#ifdef _WITH_LVS_
	conf_write(fp, "   send event to checker process = %s",
		    bfd->checker ? "Yes" : "No");
#endif
	/* If this is not at startup time, write some state variables */
	if (fp) {
		conf_write(fp, "   fd_out %d", bfd->fd_out);
		conf_write(fp, "   thread_open_fd_out 0x%p", bfd->thread_open_fd_out);
		conf_write(fp, "   thread_out 0x%p", bfd->thread_out);
		conf_write_sands(fp, "sands_out", bfd->sands_out);
		conf_write(fp, "   thread_exp 0x%p", bfd->thread_exp);
		conf_write_sands(fp, "sands_exp", bfd->sands_exp);
		conf_write(fp, "   thread_rst 0x%p", bfd->thread_rst);
		conf_write_sands(fp, "sands_rst", bfd->sands_rst);
		conf_write(fp, "   send error = %s", bfd->send_error ? "true" : "false");
		conf_write(fp, "   local state = %s", BFD_STATE_STR(bfd->local_state));
		conf_write(fp, "   remote state = %s", BFD_STATE_STR(bfd->remote_state));
		conf_write(fp, "   local discriminator = 0x%x", bfd->local_discr);
		conf_write(fp, "   remote discriminator = 0x%x", bfd->remote_discr);
		conf_write(fp, "   local diag = %s", BFD_DIAG_STR(bfd->local_diag));
		conf_write(fp, "   remote diag = %s", BFD_DIAG_STR(bfd->remote_diag));
		conf_write(fp, "   remote min tx intv = %u us", bfd->remote_min_tx_intv);
		conf_write(fp, "   remote min rx intv = %u us", bfd->remote_min_rx_intv);
		conf_write(fp, "   local demand = %u", bfd->local_demand);
		conf_write(fp, "   remote demand = %u", bfd->remote_demand);
		conf_write(fp, "   remote detect multiplier = %u", bfd->remote_detect_mult);
		conf_write(fp, "   %spoll, %sfinal", bfd->poll ? "" : "!", bfd->final ? "" : "!");
		conf_write(fp, "   local tx intv = %u us", bfd->local_tx_intv);
		conf_write(fp, "   remote tx intv = %u us", bfd->remote_tx_intv);
		conf_write(fp, "   local detection time = %" PRIu64 " us", bfd->local_detect_time);
		conf_write(fp, "   remote detection time = %" PRIu64 " us", bfd->remote_detect_time);
		if (bfd->last_seen.tv_sec == 0)
			conf_write(fp, "   last_seen = [never]");
		else {
			ctime_r(&bfd->last_seen.tv_sec, time_str);
			conf_write(fp, "   last seen = %" PRI_tv_sec ".%6.6" PRI_tv_usec " (%.24s.%6.6" PRI_tv_usec ")", bfd->last_seen.tv_sec, bfd->last_seen.tv_usec, time_str, bfd->last_seen.tv_usec);
		}
	}
}
static void
dump_bfd_list(FILE *fp, const list_head_t *l)
{
	bfd_t *bfd;

	list_for_each_entry(bfd, l, e_list)
		dump_bfd(fp, bfd);
}

/*
 *	Looks up bfd instance by name
 */
static bfd_t * __attribute__ ((pure))
find_bfd_by_name2(const char *name, const bfd_data_t *data)
{
	bfd_t *bfd;

	assert(name);
	assert(data);

	list_for_each_entry(bfd, &data->bfd, e_list) {
		if (!strcmp(name, bfd->iname))
			return bfd;
	}

	return NULL;
}

bfd_t * __attribute__ ((pure))
find_bfd_by_name(const char *name)
{
	return find_bfd_by_name2(name, bfd_data);
}

/* compares old and new timers, returns 0 if they are the same */
static int
bfd_cmp_timers(bfd_t * old_bfd, bfd_t * bfd)
{
	return (old_bfd->local_min_rx_intv != bfd->local_min_rx_intv
		|| old_bfd->local_min_tx_intv != bfd->local_min_tx_intv);
}

/*
 *	bfd_data_t functions
 */
bfd_data_t *
alloc_bfd_data(void)
{
	bfd_data_t *data;

	PMALLOC(data);
	INIT_LIST_HEAD(&data->bfd);

	/* Initialize internal variables */
	data->thread_in = NULL;
	data->fd_in = -1;
	data->multihop_fd_in = -1;

	return data;
}

void
free_bfd_data(bfd_data_t **datap)
{
	bfd_data_t *data = *datap;

	assert(data);

	free_bfd_list(&data->bfd);
	FREE(data);

	*datap = NULL;
}

void
dump_bfd_data(FILE *fp, const bfd_data_t *data)
{
	assert(data);

	dump_global_data(fp, global_data);

	if (fp) {
		conf_write(fp, "------< BFD Data >------");
		if (data->fd_in != -1)
			conf_write(fp, " fd_in = %d", data->fd_in);
		if (data->multihop_fd_in != -1)
			conf_write(fp, " multihop fd_in = %d", data->multihop_fd_in);
		conf_write(fp, " thread_in = 0x%p", data->thread_in);
	}

	if (!list_empty(&data->bfd)) {
		conf_write(fp, "------< BFD Topology >------");
		dump_bfd_list(fp, &data->bfd);
	}
}

#ifndef _ONE_PROCESS_DEBUG_
void
dump_bfd_data_global(FILE *fp)
{
	dump_bfd_data(fp, bfd_data);
}
#endif

void
bfd_print_data(void)
{
	FILE *fp;

	fp = open_dump_file("_bfd");

	if (!fp)
		return;

	dump_bfd_data(fp, bfd_data);

	fclose(fp);
}

void
bfd_complete_init(void)
{
	bfd_t *bfd, *bfd_old;

	assert(bfd_data);

	/* Build configuration */
	list_for_each_entry(bfd, &bfd_data->bfd, e_list) {
		/* If there was an old instance with the same name
		   copy its state and thread sands during reload */
		if (reload && (bfd_old = find_bfd_by_name2(bfd->iname, old_bfd_data))) {
			bfd_copy_state(bfd, bfd_old, true);
			bfd_copy_sands(bfd, bfd_old);
			if (bfd_cmp_timers(bfd_old, bfd))
				bfd_set_poll(bfd);
		} else
			bfd_init_state(bfd);
	}

	/* Copy old input fd on reload */
	if (reload) {
		bfd_data->fd_in = old_bfd_data->fd_in;
		bfd_data->multihop_fd_in = old_bfd_data->multihop_fd_in;
	}
}

/*
 *	bfd_buffer functions
 */
void
alloc_bfd_buffer(void)
{
	if (!bfd_buffer)
		bfd_buffer = (char *)MALLOC(BFD_BUFFER_SIZE);
}

void
free_bfd_buffer(void)
{
	FREE(bfd_buffer);
	bfd_buffer = NULL;
}

/*
 *	Lookup functions
 */
/* Looks up bfd instance by neighbor address and port, and optional local address.
 * If local address is not set, then it is a configuration time check and
 * the bfd instance is configured without a local address. */
bfd_t * __attribute__ ((pure))
find_bfd_by_addr(const sockaddr_t *nbr_addr, const sockaddr_t *local_addr, bool multihop)
{
	bfd_t *bfd;
	assert(nbr_addr);
	assert(local_addr);
	assert(bfd_data);

	list_for_each_entry(bfd, &bfd_data->bfd, e_list) {
		if (&bfd->nbr_addr == nbr_addr)
			continue;

		if (inet_sockaddrcmp(&bfd->nbr_addr, nbr_addr))
			continue;

		if (multihop != bfd->multihop)
			continue;

		if (!bfd->src_addr.ss_family)
			return bfd;

		if (!local_addr->ss_family) {
			/* A new bfd instance without an address is being configured,
			 * but we already have the neighbor address configured. */
			return bfd;
		}

		if (!inet_sockaddrcmp(&bfd->src_addr, local_addr))
			return bfd;
	}

	return NULL;
}

/* Looks up bfd instance by local discriminator */
bfd_t * __attribute__ ((pure))
find_bfd_by_discr(const uint32_t discr)
{
	bfd_t *bfd;

	list_for_each_entry(bfd, &bfd_data->bfd, e_list) {
		if (bfd->local_discr == discr)
			return bfd;
	}

	return NULL;
}

/*
 *	Utility functions
 */

/* Check if uint64_t is large enough to hold uint32_t * int */
#if INT_MAX <= INT32_MAX
#define CALC_TYPE uint64_t
#else
#define CALC_TYPE double
#endif

/* Generates a random number in the specified interval */
uint32_t
rand_intv(uint32_t min, uint32_t max)
{
	/* coverity[dont_call] */
	return (uint32_t)((((CALC_TYPE)max - min + 1) * random()) / (RAND_MAX + 1U)) + min;
}

#undef CALC_TYPE

/* Returns random disciminator number */
uint32_t
bfd_get_random_discr(bfd_data_t *data)
{
	bfd_t *bfd;
	uint32_t discr;

	assert(data);

	do {
		/* rand_intv(1, UINT32_MAX) only returns even numbers, since
		 * RAND_MAX == INT32_MAX == UINT32_MAX / 2 */
		discr = (rand_intv(1, UINT32_MAX) & ~1) | (time_now.tv_sec & 1);

		/* Check for collisions */
		list_for_each_entry(bfd, &data->bfd, e_list) {
			if (bfd->local_discr == discr) {
				discr = 0;
				break;
			}
		}
	} while (!discr);

	return discr;
}
