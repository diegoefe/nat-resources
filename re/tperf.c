#include "turn_cfg.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <re.h>

#include "tperf_util.h"

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
	// .proto   = IPPROTO_UDP, 
	.proto   = IPPROTO_TCP,
	.bitrate = 64000,
	.psize   = 160
};

#define PACING_INTERVAL_MS 5

static struct allocator gallocator = {
	/* .num_allocations = 100, */
	.num_allocations = 1,
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

int allocation_tx(struct allocation *alloc, struct mbuf *mb)
{
	int err;

	if (!alloc || mbuf_get_left(mb) < 4)
		return EINVAL;


	err = udp_send(alloc->us_tx, &alloc->relay, mb);

	return err;
}



int protocol_encode(struct mbuf *mb,
		    uint32_t session_cookie, uint32_t alloc_id,
		    uint32_t seq, size_t payload_len, uint8_t pattern)
{
	int err = 0;

	err |= mbuf_write_u32(mb, htonl(proto_magic));
	err |= mbuf_write_u32(mb, htonl(session_cookie));
	err |= mbuf_write_u32(mb, htonl(alloc_id));
	err |= mbuf_write_u32(mb, htonl(seq));
	err |= mbuf_write_u32(mb, htonl((uint32_t)payload_len));
	err |= mbuf_fill(mb, pattern, payload_len);

	return err;
}


 int send_packet(struct sender *snd)
{
	struct mbuf *mb = mbuf_alloc(1024);
#define PRESZ 48
	size_t payload_len;
	int err = 0;

	if (snd->psize < HDR_SIZE)
		return EINVAL;

	payload_len = snd->psize - HDR_SIZE;

	mb->pos = PRESZ;

	err = protocol_encode(mb, snd->session_cookie, snd->alloc_id,
			      ++snd->seq, payload_len, PATTERN);
	if (err)
		goto out;

	mb->pos = PRESZ;

	err = allocation_tx(snd->alloc, mb);
	if (err) {
		re_fprintf(stderr, "sender: allocation_tx(%zu bytes)"
			   " failed (%m)\n", snd->psize, err);
		goto out;
	}

	snd->total_bytes   += mbuf_get_left(mb);
	snd->total_packets += 1;

 out:
	mem_deref(mb);

	return err;
}



void sender_tick(struct sender *snd, uint64_t now)
{
	if (!snd)
		return;

	if (now >= snd->ts) {

		send_packet(snd);
		snd->ts += snd->ptime;
	}
}


void check_all_senders(struct allocator *allocator)
{
	uint64_t now = tmr_jiffies();
	struct le *le;

	for (le = allocator->allocl.head; le; le = le->next) {
		struct allocation *alloc = le->data;

		sender_tick(alloc->sender, now);
	}
}

void tmr_pace_handler(void *arg)
{
	struct allocator *allocator = arg;

	check_all_senders(allocator);

	tmr_start(&allocator->tmr_pace, PACING_INTERVAL_MS,
		  tmr_pace_handler, allocator);
}

int sender_alloc(struct sender **senderp, struct allocation *alloc,
		 uint32_t session_cookie, uint32_t alloc_id,
		 unsigned bitrate, unsigned ptime, size_t psize)
{
	struct sender *snd;
	int err = 0;

	if (!senderp || !bitrate)
		return EINVAL;

	if (ptime < PACING_INTERVAL_MS) {
		re_fprintf(stderr, "ptime %u is too low\n", ptime);
		return EINVAL;
	}
	if (psize < HDR_SIZE) {
		re_fprintf(stderr, "sender: bitrate is too low..\n");
		return EINVAL;
	}

	snd = mem_zalloc(sizeof(*snd), destructor);
	if (!snd)
		return ENOMEM;

	snd->alloc          = alloc;
	snd->session_cookie = session_cookie;
	snd->alloc_id       = alloc_id;
	snd->bitrate        = bitrate;
	snd->ptime          = ptime;
	snd->psize          = psize;

	if (err)
		mem_deref(snd);
	else
		*senderp = snd;

	return err;
}


int sender_start(struct sender *snd)
{
	if (!snd)
		return EINVAL;

	snd->ts_start = tmr_jiffies();

	/* random component to smoothe traffic */
	snd->ts       = tmr_jiffies() + rand_u16() % 100;

	return 0;
}

int print_bitrate(struct re_printf *pf, double *val)
{
	if (*val >= 1000000)
		return re_hprintf(pf, "%.2f Mbit/s", *val/1000/1000);
	else if (*val >= 1000)
		return re_hprintf(pf, "%.2f Kbit/s", *val/1000);
	else
		return re_hprintf(pf, "%.2f bit/s", *val);
}

unsigned calculate_ptime(unsigned bitrate, size_t psize)
{
	return (8 * 1000) * (unsigned)psize / bitrate;
}


int allocator_start_senders(struct allocator *allocator, unsigned bitrate,
			    size_t psize)
{
	struct le *le;
	double tbps = allocator->num_allocations * bitrate;
	unsigned ptime;
	int err = 0;

	ptime = calculate_ptime(bitrate, psize);

	re_printf("starting traffic generators:"
		  " psize=%zu, ptime=%u (total target bitrate is %H)\n",
		  psize, ptime, print_bitrate, &tbps);
	tmr_start(&allocator->tmr_ui, 1, tmr_ui_handler, allocator);

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
	/* start sending timer/thread */
	tmr_start(&allocator->tmr_pace, PACING_INTERVAL_MS,
		  tmr_pace_handler, allocator);

	return 0;
}


void allocator_print_statistics(const struct allocator *allocator)
{
	struct le *le;
	double amin = 99999999, amax = 0, asum = 0, aavg;
	int ix_min = -1, ix_max = -1;

	/* show allocation summary */
	if (!allocator || !allocator->num_sent)
		return;

	for (le = allocator->allocl.head; le; le = le->next) {

		struct allocation *alloc = le->data;

		if (alloc->atime < amin) {
			amin = alloc->atime;
			ix_min = alloc->ix;
		}
		if (alloc->atime > amax) {
			amax = alloc->atime;
			ix_max = alloc->ix;
		}

		asum += alloc->atime;
	}

	aavg = asum / allocator->num_sent;

	re_printf("\nAllocation time statistics:\n");
	re_printf("min: %.1f ms (allocation #%d)\n", amin, ix_min);
	re_printf("avg: %.1f ms\n", aavg);
	re_printf("max: %.1f ms (allocation #%d)\n", amax, ix_max);
	re_printf("\n");
}


void allocator_show_summary(const struct allocator *allocator)
{
	if (!allocator)
		return;

	if (allocator->tock > allocator->tick) {
		double duration;

		duration = (double)(allocator->tock - allocator->tick);

		re_printf("timing summary: %u allocations created in %.1f ms"
			  " (%.1f allocations per second)\n",
			  allocator->num_sent,
			  duration,
			  1.0 * allocator->num_sent / (duration / 1000.0));
	}
	else {
		re_fprintf(stderr, "duration was too short..\n");
	}

	if (allocator->num_sent)
		allocator_print_statistics(allocator);
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

		re_printf("\nall allocations are ok.\n");

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

		allocator_show_summary(allocator);

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
	// const char *host = MY_TURN_HOST;
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
	re_printf("van los mem_deref\n");
	mem_deref(turnperf.tls);
	mem_deref(turnperf.dns);
	mem_deref(dnsc);
	libre_close();

	// mem_debug();
	// tmr_debug();
	re_printf("Finalizando y retornando %d\n", err);
	return err;
}
