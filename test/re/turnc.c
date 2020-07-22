#include "turn_cfg.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <re.h>

#include "util.h"

static struct {
	const char *user, *pass;
	struct sa srv;
	int proto;
	int err;
	unsigned bitrate;
	size_t psize;
	struct tmr tmr_grace;
	struct tls *tls;
	struct stun_dns *dns;
	bool turn_ind;
} turnperf = {
	.user    = MY_TURN_USER,
	.pass    = MY_TURN_PASS,
	.proto   = IPPROTO_UDP,
	.bitrate = 64000,
	.psize   = 160
};

#define PACING_INTERVAL_MS 5

static struct allocator gallocator = {
	.num_allocations = 100,
};

void tmr_grace_handler(void *arg)
{
	(void)arg;
	re_cancel();
}


void signal_handler(int signum)
{
	static bool term = false;
	(void)signum;

	if (term) {
		re_fprintf(stderr, "forced exit\n");
		exit(2);
	}

	re_fprintf(stderr, "cancelled\n");
	term = true;

	if (gallocator.num_received > 0) {
		time_t duration = time(NULL) - gallocator.traf_start_time;

		allocator_stop_senders(&gallocator);

		re_printf("total duration: %H\n", fmt_human_time, &duration);

		re_printf("wait 1 second for traffic to settle..\n");
		tmr_start(&turnperf.tmr_grace, 1000, tmr_grace_handler, 0);
	}
	else {
		re_cancel();
	}
}

void terminate(int err)
{
	turnperf.err = err;
	re_cancel();
}

void tmr_ui_handler(void *arg)
{
	struct allocator *allocator = arg;
	time_t duration = time(NULL) - allocator->traf_start_time;

	static const char uiv[] = ".,-'-,.";
	static size_t uic = 0;

	tmr_start(&allocator->tmr_ui, 50, tmr_ui_handler, allocator);

	re_fprintf(stderr, "\r%c %H", uiv[ uic++ % (sizeof(uiv)-1) ],
		   fmt_human_time, &duration);
}

void tmr_pace_handler(void *arg)
{
	struct allocator *allocator = arg;

	//check_all_senders(allocator);

	tmr_start(&allocator->tmr_pace, PACING_INTERVAL_MS,
		  tmr_pace_handler, allocator);
}


int allocator_start_senders(struct allocator *allocator, unsigned bitrate,
			    size_t psize)
{
	struct le *le;
	double tbps = allocator->num_allocations * bitrate;
	unsigned ptime;
	int err = 0;

/*
	ptime = calculate_ptime(bitrate, psize);

	re_printf("starting traffic generators:"
		  " psize=%zu, ptime=%u (total target bitrate is %H)\n",
		  psize, ptime, print_bitrate, &tbps);
*/
	tmr_start(&allocator->tmr_ui, 1, tmr_ui_handler, allocator);
/*
	for (le = allocator->allocl.head; le; le = le->next) {
		struct allocation *alloc = le->data;

		if (alloc->sender) {
			re_fprintf(stderr, "sender already started\n");
			return EALREADY;
		}

		err = sender_alloc(&alloc->sender, alloc,
				   allocator->session_cookie,
				   alloc->ix, bitrate, ptime, psize);
		if (err)
			return err;

		err = sender_start(alloc->sender);
		if (err) {
			re_fprintf(stderr, "could not start sender (%m)", err);
			return err;
		}
	}
*/
	/* start sending timer/thread */
	tmr_start(&allocator->tmr_pace, PACING_INTERVAL_MS,
		  tmr_pace_handler, allocator);

	return 0;
}


void allocation_handler(int err, uint16_t scode, const char *reason,
			       const struct sa *srv,  const struct sa *relay,
			       void *arg)
{
	struct allocator *allocator = arg;
	(void)srv;
	(void)relay;

	if (err || scode) {
		re_fprintf(stderr, "allocation failed (%m %u %s)\n",
			   err, scode, reason);
		terminate(err ? err : EPROTO);
		return;
	}

	allocator->num_received++;

	re_fprintf(stderr, "\r[ allocations: %u ]", allocator->num_received);

	if (allocator->num_received >= allocator->num_allocations) {

		re_printf("all allocations are ok.\n");

		if (allocator->server_info) {
			re_printf("\nserver:  %s, authentication=%s\n",
				  allocator->server_software,
				  allocator->server_auth ? "yes" : "no");
			re_printf("         lifetime is %u seconds\n",
				  allocator->lifetime);
			re_printf("\n");
			re_printf("public address: %j\n",
				  &allocator->mapped_addr);
		}

		allocator->tock = tmr_jiffies();

		//allocator_show_summary(allocator);

		err = allocator_start_senders(allocator, turnperf.bitrate,
					      turnperf.psize);
		if (err) {
			re_fprintf(stderr, "failed to start senders (%m)\n",
				   err);
			terminate(err);
		}
#if 0
		tmr_debug();
#endif

		allocator->traf_start_time = time(NULL);
	}
}

void tmr_handler(void *arg)
{
	struct allocator *allocator = arg;
	unsigned i;
	int err;

	if (allocator->num_sent >= allocator->num_allocations) {
		return;
	}

	i = allocator->num_sent;

	err = allocation_create(allocator, i, turnperf.proto, &turnperf.srv,
				turnperf.user, turnperf.pass,
				turnperf.tls, turnperf.turn_ind,
				allocation_handler, allocator);
	if (err) {
		re_fprintf(stderr, "creating allocation number %u failed"
			   " (%m)\n", i, err);
		goto out;
	}

	allocator->num_sent++;

	tmr_start(&allocator->tmr, rand_u16()&3, tmr_handler, allocator);

 out:
	if (err)
		terminate(err);
}

void allocator_start(struct allocator *allocator)
{
	if (!allocator)
		return;

	allocator->tick = tmr_jiffies();
	tmr_start(&allocator->tmr, 0, tmr_handler, allocator);
}


void dns_handler(int err, const struct sa *srv, void *arg)
{
	(void)arg;

	if (err)
		goto out;

	re_printf("resolved TURN-server: %J\n", srv);

	turnperf.srv = *srv;

	/* create a bunch of allocations, with timing */
	allocator_start(&gallocator);

 out:
	if (err)
		terminate(err);
}

int main() {
	const char *host = MY_TURN_HOST;
	struct dnsc *dnsc = NULL;
	int maxfds = 4096;
	uint64_t dport = STUN_PORT;
	uint16_t port = 0;
	bool secure = false;
	int err = libre_init();
	if(err) {
		re_fprintf(stderr, "re init failed: %s\n", strerror(err));
		goto out;
	}

	enum poll_method method = poll_method_best();
	switch (method) {
		case METHOD_SELECT:
			maxfds = 1024;
			break;
		default:
			maxfds = 32768;
			break;
	}
	err = fd_setsize(maxfds);
	if (err) {
		re_fprintf(stderr, "cannot set maxfds to %d: %m\n",
			   maxfds, err);
		goto out;
	}

	err = poll_method_set(method);
	if (err) {
		re_fprintf(stderr, "could not set polling method '%s' (%m)\n",
			   poll_method_name(method), err);
		goto out;
	}

	re_printf("using async polling method '%s' with maxfds=%d\n",
		  poll_method_name(method), maxfds);

	err = dns_init(&dnsc);
	if (err) {
		(void)re_fprintf(stderr, "dnsinit: %m\n", err);
		goto out;
	}

	re_printf("bitrate: %u bits/second (per allocation)\n",  turnperf.bitrate);

	re_printf("server: %s protocol=%s\n",
			  host, protocol_name(turnperf.proto, secure));

	const char *stun_proto, *stun_usage;
	stun_usage = secure ? stuns_usage_relay : stun_usage_relay;

	switch (turnperf.proto) {

	case IPPROTO_UDP:
		stun_proto = stun_proto_udp;
		break;

	case IPPROTO_TCP:
		stun_proto = stun_proto_tcp;
		break;

	default:
		err = EPROTONOSUPPORT;
		goto out;
	}

	err = stun_server_discover(&turnperf.dns, dnsc,
										stun_usage, stun_proto,
										AF_INET, host, port,
										dns_handler, NULL);
	if (err) {
		re_fprintf(stderr, "stun discover failed (%m)\n",
				err);
		goto out;
	}
	
	re_main(signal_handler);

	if (turnperf.err) {
		re_fprintf(stderr, "turn performance failed (%m)\n",
			   turnperf.err);
		goto out;
	}

 out:
	mem_deref(turnperf.tls);
	mem_deref(turnperf.dns);
	mem_deref(dnsc);
	libre_close();

	mem_debug();
	tmr_debug();
	return err;
}
