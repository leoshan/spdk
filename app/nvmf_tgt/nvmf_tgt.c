/*-
 *   BSD LICENSE
 *
 *   Copyright (c) Intel Corporation.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

#include <rte_config.h>
#include <rte_lcore.h>

#include "nvmf_tgt.h"

#include "spdk/bdev.h"
#include "spdk/event.h"
#include "spdk/log.h"
#include "spdk/nvme.h"

#define SPDK_NVMF_BUILD_ETC "/usr/local/etc/nvmf"
#define SPDK_NVMF_DEFAULT_CONFIG SPDK_NVMF_BUILD_ETC "/nvmf.conf"

static struct spdk_poller *g_acceptor_poller = NULL;

static TAILQ_HEAD(, nvmf_tgt_subsystem) g_subsystems = TAILQ_HEAD_INITIALIZER(g_subsystems);
static bool g_subsystems_shutdown;

static void
shutdown_complete(void)
{
	spdk_app_stop(0);
}

static void
subsystem_delete_event(void *arg1, void *arg2)
{
	struct nvmf_tgt_subsystem *app_subsys = arg1;
	struct spdk_nvmf_subsystem *subsystem = app_subsys->subsystem;

	TAILQ_REMOVE(&g_subsystems, app_subsys, tailq);
	free(app_subsys);

	spdk_nvmf_delete_subsystem(subsystem);

	if (g_subsystems_shutdown && TAILQ_EMPTY(&g_subsystems)) {
		spdk_nvmf_tgt_fini();
		/* Finished shutting down all subsystems - continue the shutdown process. */
		shutdown_complete();
	}
}

static void
nvmf_tgt_delete_subsystem(struct nvmf_tgt_subsystem *app_subsys)
{
	struct spdk_event *event;

	/*
	 * Unregister the poller - this starts a chain of events that will eventually free
	 * the subsystem's memory.
	 */
	event = spdk_event_allocate(spdk_app_get_current_core(), subsystem_delete_event,
				    app_subsys, NULL);
	spdk_poller_unregister(&app_subsys->poller, event);
}

static void
shutdown_subsystems(void)
{
	struct nvmf_tgt_subsystem *app_subsys, *tmp;

	g_subsystems_shutdown = true;
	TAILQ_FOREACH_SAFE(app_subsys, &g_subsystems, tailq, tmp) {
		nvmf_tgt_delete_subsystem(app_subsys);
	}
}

static void
acceptor_poller_unregistered_event(void *arg1, void *arg2)
{
	shutdown_subsystems();
}

static void
spdk_nvmf_shutdown_cb(void)
{
	struct spdk_event *event;

	fprintf(stdout, "\n=========================\n");
	fprintf(stdout, "   NVMF shutdown signal\n");
	fprintf(stdout, "=========================\n");

	event = spdk_event_allocate(spdk_app_get_current_core(), acceptor_poller_unregistered_event,
				    NULL, NULL);
	spdk_poller_unregister(&g_acceptor_poller, event);
}

static void
subsystem_poll(void *arg)
{
	struct nvmf_tgt_subsystem *app_subsys = arg;

	spdk_nvmf_subsystem_poll(app_subsys->subsystem);
}

static void
connect_event(void *arg1, void *arg2)
{
	struct spdk_nvmf_request *req = arg1;

	spdk_nvmf_handle_connect(req);
}

static void
connect_cb(void *cb_ctx, struct spdk_nvmf_request *req)
{
	struct nvmf_tgt_subsystem *app_subsys = cb_ctx;
	struct spdk_event *event;

	/* Pass an event to the lcore that owns this subsystem */
	event = spdk_event_allocate(app_subsys->lcore, connect_event, req, NULL);
	spdk_event_call(event);
}

static void
disconnect_event(void *arg1, void *arg2)
{
	struct spdk_nvmf_conn *conn = arg1;

	spdk_nvmf_session_disconnect(conn);
}

static void
disconnect_cb(void *cb_ctx, struct spdk_nvmf_conn *conn)
{
	struct nvmf_tgt_subsystem *app_subsys = cb_ctx;
	struct spdk_event *event;

	/* Pass an event to the core that owns this connection */
	event = spdk_event_allocate(app_subsys->lcore, disconnect_event, conn, NULL);
	spdk_event_call(event);
}

static void
_nvmf_tgt_start_subsystem(void *arg1, void *arg2)
{
	struct nvmf_tgt_subsystem *app_subsys = arg1;
	struct spdk_nvmf_subsystem *subsystem = app_subsys->subsystem;
	int lcore = spdk_app_get_current_core();

	spdk_nvmf_subsystem_start(subsystem);

	spdk_poller_register(&app_subsys->poller, subsystem_poll, app_subsys, lcore, 0);
}

void
nvmf_tgt_start_subsystem(struct nvmf_tgt_subsystem *app_subsys)
{
	struct spdk_event *event;

	event = spdk_event_allocate(app_subsys->lcore, _nvmf_tgt_start_subsystem,
				    app_subsys, NULL);
	spdk_event_call(event);
}

struct nvmf_tgt_subsystem *
nvmf_tgt_create_subsystem(const char *name, enum spdk_nvmf_subtype subtype,
			  enum spdk_nvmf_subsystem_mode mode, uint32_t lcore)
{
	struct spdk_nvmf_subsystem *subsystem;
	struct nvmf_tgt_subsystem *app_subsys;

	if (spdk_nvmf_subsystem_exists(name)) {
		SPDK_ERRLOG("Subsystem already exist\n");
		return NULL;
	}

	app_subsys = calloc(1, sizeof(*app_subsys));
	if (app_subsys == NULL) {
		SPDK_ERRLOG("Subsystem allocation failed\n");
		return NULL;
	}

	subsystem = spdk_nvmf_create_subsystem(name, subtype, mode, app_subsys, connect_cb,
					       disconnect_cb);
	if (subsystem == NULL) {
		SPDK_ERRLOG("Subsystem creation failed\n");
		free(app_subsys);
		return NULL;
	}

	app_subsys->subsystem = subsystem;
	app_subsys->lcore = lcore;

	SPDK_NOTICELOG("allocated subsystem %s on lcore %u on socket %u\n", name, lcore,
		       rte_lcore_to_socket_id(lcore));

	TAILQ_INSERT_TAIL(&g_subsystems, app_subsys, tailq);

	return app_subsys;
}

/* This function can only be used before the pollers are started. */
static void
nvmf_tgt_delete_subsystems(void)
{
	struct nvmf_tgt_subsystem *app_subsys, *tmp;
	struct spdk_nvmf_subsystem *subsystem;

	TAILQ_FOREACH_SAFE(app_subsys, &g_subsystems, tailq, tmp) {
		TAILQ_REMOVE(&g_subsystems, app_subsys, tailq);
		subsystem = app_subsys->subsystem;
		spdk_nvmf_delete_subsystem(subsystem);
		free(app_subsys);
	}
}

struct nvmf_tgt_subsystem *
nvmf_tgt_subsystem_first(void)
{
	return TAILQ_FIRST(&g_subsystems);
}

struct nvmf_tgt_subsystem *
nvmf_tgt_subsystem_next(struct nvmf_tgt_subsystem *subsystem)
{
	return TAILQ_NEXT(subsystem, tailq);
}

int
nvmf_tgt_shutdown_subsystem_by_nqn(const char *nqn)
{
	struct nvmf_tgt_subsystem *tgt_subsystem, *subsys_tmp;

	TAILQ_FOREACH_SAFE(tgt_subsystem, &g_subsystems, tailq, subsys_tmp) {
		if (strcmp(tgt_subsystem->subsystem->subnqn, nqn) == 0) {
			nvmf_tgt_delete_subsystem(tgt_subsystem);
			return 0;
		}
	}
	return -1;
}

static void
usage(void)
{
	struct spdk_app_opts opts;

	spdk_app_opts_init(&opts);

	printf("nvmf [options]\n");
	printf("options:\n");
	printf(" -c config  - config file (default %s)\n", SPDK_NVMF_DEFAULT_CONFIG);
	printf(" -e mask    - tracepoint group mask for spdk trace buffers (default 0x0)\n");
	printf(" -m mask    - core mask for DPDK\n");
	printf(" -i shared memory ID (optional)\n");
	printf(" -l facility - use specific syslog facility (default %s)\n",
	       opts.log_facility);
	printf(" -n channel number of memory channels used for DPDK\n");
	printf(" -p core    master (primary) core for DPDK\n");
	printf(" -s size    memory size in MB for DPDK\n");

	spdk_tracelog_usage(stdout, "-t");

	printf(" -v         - verbose (enable warnings)\n");
	printf(" -H         - show this usage\n");
	printf(" -d         - disable coredump file enabling\n");
}

static void
acceptor_poll(void *arg)
{
	spdk_nvmf_acceptor_poll();
}

static void
spdk_nvmf_startup(void *arg1, void *arg2)
{
	int rc;

	rc = spdk_nvmf_parse_conf();
	if (rc < 0) {
		SPDK_ERRLOG("spdk_nvmf_parse_conf() failed\n");
		goto initialize_error;
	}

	if (((1ULL << g_spdk_nvmf_tgt_conf.acceptor_lcore) & spdk_app_get_core_mask()) == 0) {
		SPDK_ERRLOG("Invalid AcceptorCore setting\n");
		goto initialize_error;
	}

	spdk_poller_register(&g_acceptor_poller, acceptor_poll, NULL,
			     g_spdk_nvmf_tgt_conf.acceptor_lcore,
			     g_spdk_nvmf_tgt_conf.acceptor_poll_rate);

	SPDK_NOTICELOG("Acceptor running on core %u on socket %u\n", g_spdk_nvmf_tgt_conf.acceptor_lcore,
		       rte_lcore_to_socket_id(g_spdk_nvmf_tgt_conf.acceptor_lcore));

	if (getenv("MEMZONE_DUMP") != NULL) {
		spdk_memzone_dump(stdout);
		fflush(stdout);
	}

	return;

initialize_error:
	nvmf_tgt_delete_subsystems();
	spdk_app_stop(rc);
}

/*! \file

This is the main file.

*/

/*!

\brief This is the main function for the NVMf target application.

\msc

	c_runtime [label="C Runtime"], nvmf [label="NVMf target"];
	c_runtime=>nvmf [label="main()"];
	nvmf=>nvmf [label="spdk_app_init()"];
	nvmf=>nvmf [label="spdk_event_allocate()"];
	nvmf=>nvmf [label="spdk_app_start()"];
	nvmf=>nvmf [label="spdk_app_fini()"];
	c_runtime<<nvmf;

\endmsc

*/

int
main(int argc, char **argv)
{
	int ch;
	int rc;
	struct spdk_app_opts opts = {};

	/* default value in opts */
	spdk_app_opts_init(&opts);

	opts.name = "nvmf";
	opts.config_file = SPDK_NVMF_DEFAULT_CONFIG;
	opts.max_delay_us = 1000; /* 1 ms */

	while ((ch = getopt(argc, argv, "c:de:i:l:m:n:p:qs:t:DH")) != -1) {
		switch (ch) {
		case 'd':
			opts.enable_coredump = false;
			break;
		case 'c':
			opts.config_file = optarg;
			break;
		case 'i':
			opts.shm_id = atoi(optarg);
			break;
		case 'l':
			opts.log_facility = optarg;
			break;
		case 't':
			rc = spdk_log_set_trace_flag(optarg);
			if (rc < 0) {
				fprintf(stderr, "unknown flag\n");
				usage();
				exit(EXIT_FAILURE);
			}
#ifndef DEBUG
			fprintf(stderr, "%s must be rebuilt with CONFIG_DEBUG=y for -t flag.\n",
				argv[0]);
			usage();
			exit(EXIT_FAILURE);
#endif
			break;
		case 'm':
			opts.reactor_mask = optarg;
			break;
		case 'n':
			opts.dpdk_mem_channel = atoi(optarg);
			break;
		case 'p':
			opts.dpdk_master_core = atoi(optarg);
			break;
		case 's':
			opts.dpdk_mem_size = atoi(optarg);
			break;
		case 'e':
			opts.tpoint_group_mask = optarg;
			break;
		case 'q':
			spdk_g_notice_stderr_flag = 0;
			break;
		case 'D':
		case 'H':
		default:
			usage();
			exit(EXIT_SUCCESS);
		}
	}

	if (spdk_g_notice_stderr_flag == 1 &&
	    isatty(STDERR_FILENO) &&
	    !strncmp(ttyname(STDERR_FILENO), "/dev/tty", strlen("/dev/tty"))) {
		printf("Warning: printing stderr to console terminal without -q option specified.\n");
		printf("Suggest using -q to disable logging to stderr and monitor syslog, or\n");
		printf("redirect stderr to a file.\n");
		printf("(Delaying for 10 seconds...)\n");
		sleep(10);
	}

	opts.shutdown_cb = spdk_nvmf_shutdown_cb;
	spdk_app_init(&opts);

	printf("Total cores available: %d\n", rte_lcore_count());
	/* Blocks until the application is exiting */
	rc = spdk_app_start(spdk_nvmf_startup, NULL, NULL);

	spdk_app_fini();

	return rc;
}
