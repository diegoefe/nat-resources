#include "turn_cfg.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
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

	#if 0
	if (gallocator.num_received > 0) {
		time_t duration = time(NULL) - gallocator.traf_start_time;

		allocator_stop_senders(&gallocator);

		re_printf("total duration: %H\n", fmt_human_time, &duration);

		re_printf("wait 1 second for traffic to settle..\n");
		tmr_start(&turnperf.tmr_grace, 1000, tmr_grace_handler, 0);
	}
	else {
	#endif
		re_cancel();
	//}
}


void terminate(int err)
{
	turnperf.err = err;
	re_cancel();
}

void dns_handler(int err, const struct sa *srv, void *arg)
{
	(void)arg;

	if (err)
		goto out;

	re_printf("resolved TURN-server: %J\n", srv);

	turnperf.srv = *srv;

	/* create a bunch of allocations, with timing */
	// allocator_start(&gallocator);

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
