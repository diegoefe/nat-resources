#include "turn_cfg.h"

#include <stdio.h>
#include <string.h>
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


int main() {
	const char *host = MY_TURN_HOST;
	struct dnsc *dnsc = NULL;
	int maxfds = 4096;
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
	

#if 0
	struct turnc* sTC;

	err = turnc_alloc(&sTC, const struct stun_conf *conf, int proto,
		void *sock, int layer, const struct sa *srv,
		MY_TURN_USER, MY_TURN_PASS,
		uint32_t lifetime, turnc_h *th, void *arg);

#endif

 out:
	// mem_deref(sTC);
	mem_deref(dnsc);
	libre_close();

	mem_debug();
	tmr_debug();
	return err;
}
