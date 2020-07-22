#include "util.h"

#include <sys/time.h>
#include <string.h>

enum {
	TURN_LAYER = 0,
	DTLS_LAYER = -100,
};

enum {
	PING_INTERVAL = 5000,
	REDIRC_MAX = 16,
};

static const uint32_t proto_magic = 'T'<<24 | 'P'<<16 | 'R'<<8 | 'F';

void destructor(void *arg)
{
	struct allocation *alloc = arg;

	list_unlink(&alloc->le);

	tmr_cancel(&alloc->tmr_ping);

	mem_deref(alloc->sender);

	/* note: order matters */
 	mem_deref(alloc->turnc);     /* close TURN client, to de-allocate */
	mem_deref(alloc->dtls_sock);
	mem_deref(alloc->us);        /* must be closed after TURN client */

	mem_deref(alloc->tlsc);
	mem_deref(alloc->tc);
	mem_deref(alloc->mb);
	mem_deref(alloc->us_tx);

	mem_deref(alloc->tls);
}

void receiver_init(struct receiver *recvr,
		   uint32_t exp_cookie, uint32_t exp_allocid)
{
	if (!recvr)
		return;

	memset(recvr, 0, sizeof(*recvr));

	recvr->cookie = exp_cookie;
	recvr->allocid = exp_allocid;
}

int allocation_create(struct allocator *allocator, unsigned ix, int proto,
		      const struct sa *srv,
		      const char *username, const char *password,
		      struct tls *tls, bool turn_ind,
		      allocation_h *alloch, void *arg)
{
	struct allocation *alloc;
	struct sa laddr;
	int err;

	if (!allocator || !proto || !srv)
		return EINVAL;

	sa_init(&laddr, sa_af(srv));

	alloc = mem_zalloc(sizeof(*alloc), destructor);
	if (!alloc)
		return ENOMEM;

	list_append(&allocator->allocl, &alloc->le, alloc);

	(void)gettimeofday(&alloc->sent, NULL);

	alloc->atime     = -1;
	alloc->ix        = ix;
	alloc->allocator = allocator;
	alloc->proto     = proto;
	alloc->secure    = tls != NULL;
	alloc->srv       = *srv;
	alloc->user      = username;
	alloc->pass      = password;
	alloc->turn_ind  = turn_ind;
	alloc->alloch    = alloch;
	alloc->arg       = arg;
	alloc->tls       = mem_ref(tls);

	receiver_init(&alloc->recv, allocator->session_cookie, alloc->ix);

	err = udp_listen(&alloc->us_tx, &laddr, NULL, NULL);
	if (err) {
		re_fprintf(stderr, "allocation: failed to create UDP tx socket"
			   " (%m)\n", err);
		goto out;
	}

	udp_local_get(alloc->us_tx, &alloc->laddr_tx);

	err = start(alloc);
	if (err)
		goto out;

 out:
	if (err)
		mem_deref(alloc);

	return err;
}


void tmr_ping_handler(void *arg)
{
	struct allocation *alloc = arg;
	struct mbuf *mb;

	tmr_start(&alloc->tmr_ping, PING_INTERVAL, tmr_ping_handler, alloc);

	mb = mbuf_alloc(256);
	if (!mb)
		return;

	mb->pos = 48;
	mbuf_write_str(mb, "PING");
	mb->pos = 48;

	turnc_send(alloc->turnc, &alloc->peer, mb);

	mem_deref(mb);
}

bool is_connection_oriented(const struct allocation *alloc)
{
	return alloc->proto == IPPROTO_TCP ||
		(alloc->proto == IPPROTO_UDP && alloc->secure);
}

void perm_handler(void *arg)
{
	struct allocation *alloc = arg;

	re_printf("%s to %J added.\n",
		  alloc->turn_ind ? "Permission" : "Channel",
		  &alloc->peer);

	alloc->alloch(0, 0, "OK", &alloc->srv, &alloc->relay, alloc->arg);
}

int set_peer(struct allocation *alloc, const struct sa *peer)
{
	alloc->peer = *peer;

	tmr_start(&alloc->tmr_ping, PING_INTERVAL, tmr_ping_handler, alloc);

	if (alloc->turn_ind)
		return turnc_add_perm(alloc->turnc, peer, perm_handler, alloc);
	else
		return turnc_add_chan(alloc->turnc, peer, perm_handler, alloc);
}

void turnc_handler(int err, uint16_t scode, const char *reason,
			  const struct sa *relay_addr,
			  const struct sa *mapped_addr,
			  const struct stun_msg *msg,
			  void *arg)
{
	struct allocation *alloc = arg;
	struct allocator *allocator = alloc->allocator;
	struct timeval now;
	struct sa peer;

	if (err) {
		(void)re_fprintf(stderr, "[%u] turn error: %m\n",
				 alloc->ix, err);
		alloc->err = err;
		goto term;
	}

	if (scode) {

		if (scode == 300 && is_connection_oriented(alloc) &&
		    alloc->redirc++ < REDIRC_MAX) {

			const struct stun_attr *alt;

			alt = stun_msg_attr(msg, STUN_ATTR_ALT_SERVER);
			if (!alt)
				goto term;

			re_printf("[%u] redirecting to new server %J\n",
				  alloc->ix, &alt->v.alt_server);

			alloc->srv = alt->v.alt_server;

			alloc->turnc = mem_deref(alloc->turnc);
			alloc->tlsc  = mem_deref(alloc->tlsc);
			alloc->tc    = mem_deref(alloc->tc);
			alloc->dtls_sock = mem_deref(alloc->dtls_sock);
			alloc->us    = mem_deref(alloc->us);

			err = start(alloc);
			if (err)
				goto term;

			return;
		}

		(void)re_fprintf(stderr, "[%u] turn error: %u %s\n",
				 alloc->ix, scode, reason);
		alloc->err = EPROTO;
		goto term;
	}

	if (sa_af(relay_addr) != sa_af(mapped_addr)) {
		re_fprintf(stderr, "allocation: address-family mismatch"
			   " (mapped=%J, relay=%J)\n",
			   mapped_addr, relay_addr);
		err = EAFNOSUPPORT;
		goto term;
	}

	alloc->ok = true;
	alloc->relay = *relay_addr;

	(void)gettimeofday(&now, NULL);

	alloc->atime  = (double)(now.tv_sec - alloc->sent.tv_sec) * 1000;
	alloc->atime += (double)(now.tv_usec - alloc->sent.tv_usec) / 1000;

	/* save information from the TURN server */
	if (!allocator->server_info) {

		struct stun_attr *attr;

		allocator->server_auth =
			(NULL != stun_msg_attr(msg, STUN_ATTR_MSG_INTEGRITY));

		attr = stun_msg_attr(msg, STUN_ATTR_SOFTWARE);
		if (attr) {
			str_ncpy(allocator->server_software, attr->v.software,
				 sizeof(allocator->server_software));
		}

		allocator->mapped_addr = *mapped_addr;

		allocator->server_info = true;

		attr = stun_msg_attr(msg, STUN_ATTR_LIFETIME);
		if (attr) {
			allocator->lifetime = attr->v.lifetime;
		}
	}

	peer = *mapped_addr;
	sa_set_port(&peer, sa_port(&alloc->laddr_tx));

	err = set_peer(alloc, &peer);
	if (err)
		goto term;

	return;

 term:
	alloc->alloch(err, scode, reason, NULL, NULL, alloc->arg);
}


void dtls_estab_handler(void *arg)
{
	struct allocation *alloc = arg;
	int err;

	re_printf("allocation: DTLS established\n");

	err = turnc_alloc(&alloc->turnc, NULL, STUN_TRANSP_DTLS,
			  alloc->tlsc, TURN_LAYER,
			  &alloc->srv, alloc->user, alloc->pass,
			  TURN_DEFAULT_LIFETIME, turnc_handler, alloc);
	if (err) {
		re_fprintf(stderr, "allocation: failed to"
			   " create TURN client"
			   " (%m)\n", err);
		goto out;
	}

 out:
	if (err)
		alloc->alloch(err, 0, NULL, NULL, NULL, alloc->arg);
}


int dns_init(struct dnsc **dnsc)
{
	struct sa nsv[8];
	uint32_t nsn;
	int err;

	nsn = ARRAY_SIZE(nsv);

	err = dns_srv_get(NULL, 0, nsv, &nsn);
	if (err) {
		(void)re_fprintf(stderr, "dns_srv_get: %m\n", err);
		goto out;
	}

	err = dnsc_alloc(dnsc, NULL, nsv, nsn);
	if (err) {
		(void)re_fprintf(stderr, "dnsc_alloc: %m\n", err);
		goto out;
	}

 out:
	return err;
}

const char *protocol_name(int proto, bool secure)
{
	if (secure) {
		switch (proto) {

		case IPPROTO_UDP: return "DTLS";
		case IPPROTO_TCP: return "TLS";
		default: return "???";
		}
	}
	else {
		return net_proto2name(proto);
	}
}

void sender_stop(struct sender *snd)
{
	if (!snd)
		return;

	snd->ts_stop = tmr_jiffies();
}


void allocator_stop_senders(struct allocator *allocator)
{
	struct le *le;

	if (!allocator)
		return;

	tmr_cancel(&allocator->tmr_ui);
	tmr_cancel(&allocator->tmr_pace);
	for (le = allocator->allocl.head; le; le = le->next) {
		struct allocation *alloc = le->data;

		sender_stop(alloc->sender);
	}
}


int protocol_decode(struct hdr *hdr, struct mbuf *mb)
{
	uint32_t magic;
	size_t start;
	int err = 0;

	if (!hdr || !mb)
		return EINVAL;

	start = mb->pos;

	if (mbuf_get_left(mb) < HDR_SIZE)
		return EBADMSG;

	magic = ntohl(mbuf_read_u32(mb));
	if (magic != proto_magic) {
		err = EBADMSG;
		goto out;
	}

	hdr->session_cookie = ntohl(mbuf_read_u32(mb));
	hdr->alloc_id       = ntohl(mbuf_read_u32(mb));
	hdr->seq            = ntohl(mbuf_read_u32(mb));
	hdr->payload_len    = ntohl(mbuf_read_u32(mb));

	if (mbuf_get_left(mb) < hdr->payload_len) {
		re_fprintf(stderr, "receiver: header said %zu bytes,"
			   " but payload is only %zu bytes\n",
			   hdr->payload_len, mbuf_get_left(mb));
		err = EPROTO;
		goto out;
	}

	/* save portions of the packet */
	memcpy(hdr->payload, mbuf_buf(mb),
	       min (mbuf_get_left(mb), sizeof(hdr->payload)));

	/* important, so that the TURN TCP-framing works */
	mbuf_advance(mb, hdr->payload_len);

 out:
	if (err)
		mb->pos = start;

	return 0;
}


void protocol_packet_dump(const struct hdr *hdr)
{
	if (!hdr)
		return;

	re_fprintf(stderr, "--- protocol packet: ---\n");
	re_fprintf(stderr, "session_cookie: 0x%08x\n", hdr->session_cookie);
	re_fprintf(stderr, "alloc_id:       %u\n", hdr->alloc_id);
	re_fprintf(stderr, "seq:            %u\n", hdr->seq);
	re_fprintf(stderr, "payload_len:    %u\n", hdr->payload_len);
	re_fprintf(stderr, "payload:        %w\n",
		   hdr->payload, hdr->payload_len);
	re_fprintf(stderr, "\n");
}


int receiver_recv(struct receiver *recvr,
		  const struct sa *src, struct mbuf *mb)
{
	struct hdr hdr;
	uint64_t now = tmr_jiffies();
	size_t start, sz;
	int err;

	if (!recvr || !mb)
		return EINVAL;

	if (!recvr->ts_start)
		recvr->ts_start = now;
	recvr->ts_last = now;

	start = mb->pos;
	sz = mbuf_get_left(mb);

	/* decode packet */
	err = protocol_decode(&hdr, mb);
	if (err) {
		if (err == EBADMSG) {
			re_fprintf(stderr, "[%u] ignore a non-Turnperf packet"
				   " from %J (%zu bytes)\n",
				   recvr->allocid, src, sz);
			hexdump(stderr, mb->buf + start, sz);
			return 0;
		}

		re_fprintf(stderr, "receiver: protocol decode"
			   " error [%zu bytes from %J] (%m)\n", sz, src, err);
		re_fprintf(stderr, "          %w\n", mb->buf + start, sz);
		return err;
	}

	/* verify packet */
	if (hdr.session_cookie != recvr->cookie) {
		re_fprintf(stderr, "invalid cookie received"
			   " from %J [exp=%x, actual=%x] (%zu bytes)\n",
			   src, recvr->cookie, hdr.session_cookie, sz);
		protocol_packet_dump(&hdr);
		return EPROTO;
	}
	if (hdr.alloc_id != recvr->allocid) {
		re_fprintf(stderr, "invalid allocation-ID received"
			   " from %J [exp=%u, actual=%u] (%zu bytes)\n",
			   src, hdr.alloc_id, recvr->allocid, sz);
		protocol_packet_dump(&hdr);
		return EPROTO;
	}

	if (recvr->last_seq) {
		if (hdr.seq <= recvr->last_seq) {
			re_fprintf(stderr, "receiver[%u]: late or "
				   " out-of-order packet from %J"
				   " (last_seq=%u, seq=%u)\n",
				   recvr->allocid, src,
				   recvr->last_seq, hdr.seq);
		}
	}

#if 0
	protocol_packet_dump(&hdr);
#endif

	recvr->total_bytes   += sz;
	recvr->total_packets += 1;

	recvr->last_seq = hdr.seq;

	return 0;
}


void data_handler(struct allocation *alloc, const struct sa *src,
			 struct mbuf *mb)
{
	int err;

	if (!alloc->ok) {
		re_fprintf(stderr, "allocation not ready"
			   " -- ignore %zu bytes from %J\n",
			   mbuf_get_left(mb), src);
		return;
	}

	if (!sa_cmp(src, &alloc->peer, SA_ALL)) {

		re_printf("updating peer address:  %J  -->  %J\n",
			  &alloc->peer, src);

		alloc->peer = *src;

		if (!alloc->turn_ind)
			turnc_add_chan(alloc->turnc, src, NULL, NULL);

		tmr_start(&alloc->tmr_ping, PING_INTERVAL,
			  tmr_ping_handler, alloc);
	}

	err = receiver_recv(&alloc->recv, src, mb);
	if (err) {
		re_fprintf(stderr, "corrupt packet coming from %J (%m)\n",
			   src, err);
	}
}

void dtls_recv_handler(struct mbuf *mb, void *arg)
{
	struct allocation *alloc = arg;
	struct sa src;
	int err;

	/* forward packet to TURN-client */
	err = turnc_recv(alloc->turnc, &src, mb);
	if (err) {
		alloc->alloch(err, 0, NULL, NULL, NULL, alloc->arg);
		return;
	}

	/* available application data? */
	if (mbuf_get_left(mb)) {
		data_handler(alloc, &src, mb);
	}
}

void dtls_close_handler(int err, void *arg)
{
	struct allocation *alloc = arg;

	re_fprintf(stderr, "dtls: close (%m)\n", err);

	alloc->alloch(err ? err : ECONNRESET, 0, NULL, NULL, NULL, alloc->arg);
}

void udp_recv(const struct sa *src, struct mbuf *mb, void *arg)
{
	struct allocation *alloc = arg;

	data_handler(alloc, src, mb);
}

void tcp_estab_handler(void *arg)
{
	struct allocation *alloc = arg;
	int err;

	re_printf("allocation: TCP established\n");

	alloc->mb = mem_deref(alloc->mb);

	err = turnc_alloc(&alloc->turnc, NULL, IPPROTO_TCP, alloc->tc, 0,
			  &alloc->srv, alloc->user, alloc->pass,
			  TURN_DEFAULT_LIFETIME, turnc_handler, alloc);
	if (err)
		alloc->alloch(err, 0, NULL, NULL, NULL, alloc->arg);
}

void tcp_recv_handler(struct mbuf *mb_pkt, void *arg)
{
	struct allocation *alloc = arg;
	int err = 0;

	re_printf("TCP received\n");
	/* re-assembly of fragments */
	if (alloc->mb) {
		size_t pos;

		pos = alloc->mb->pos;

		alloc->mb->pos = alloc->mb->end;

		err = mbuf_write_mem(alloc->mb,
				     mbuf_buf(mb_pkt), mbuf_get_left(mb_pkt));
		if (err)
			goto out;

		alloc->mb->pos = pos;
	}
	else {
		alloc->mb = mem_ref(mb_pkt);
	}

	for (;;) {

		size_t len, pos, end;
		struct sa src;
		uint16_t typ;

		if (mbuf_get_left(alloc->mb) < 4)
			break;

		typ = ntohs(mbuf_read_u16(alloc->mb));
		len = ntohs(mbuf_read_u16(alloc->mb));

		if (typ < 0x4000)
			len += STUN_HEADER_SIZE;
		else if (typ < 0x8000)
			len += 4;
		else {
			err = EBADMSG;
			goto out;
		}

		alloc->mb->pos -= 4;

		if (mbuf_get_left(alloc->mb) < len)
			break;

		pos = alloc->mb->pos;
		end = alloc->mb->end;

		alloc->mb->end = pos + len;

		/* forward packet to TURN client */
		err = turnc_recv(alloc->turnc, &src, alloc->mb);
		if (err)
			goto out;

		if (mbuf_get_left(alloc->mb)) {
			data_handler(alloc, &src, alloc->mb);
		}

		/* 4 byte alignment */
		while (len & 0x03)
			++len;

		alloc->mb->pos = pos + len;
		alloc->mb->end = end;

		if (alloc->mb->pos >= alloc->mb->end) {
			alloc->mb = mem_deref(alloc->mb);
			break;
		}
	}

 out:
	if (err) {
		alloc->alloch(err, 0, NULL, NULL, NULL, alloc->arg);
	}
}

void tcp_close_handler(int err, void *arg)
{
	struct allocation *alloc = arg;

	alloc->alloch(err ? err : ECONNRESET, 0, NULL, NULL, NULL, alloc->arg);
}

int start(struct allocation *alloc)
{
	struct sa laddr;
	int err = 0;

	if (!alloc)
		return EINVAL;

	sa_init(&laddr, sa_af(&alloc->srv));

	switch (alloc->proto) {

	case IPPROTO_UDP:
		err = udp_listen(&alloc->us, &laddr, udp_recv, alloc);
		if (err) {
			re_fprintf(stderr, "allocation: failed to"
				   " create UDP socket"
				   " (%m)\n", err);
			goto out;
		}

		udp_sockbuf_set(alloc->us, 524288);

		if (alloc->secure) {

			/* note: re-using UDP socket for DTLS-traffic */
			err = dtls_listen(&alloc->dtls_sock, NULL, alloc->us,
					  2, DTLS_LAYER, NULL, NULL);
			if (err) {
				re_fprintf(stderr, "dtls_listen error: %m\n",
					   err);
				goto out;
			}

			err = dtls_connect(&alloc->tlsc, alloc->tls,
					   alloc->dtls_sock, &alloc->srv,
					   dtls_estab_handler,
					   dtls_recv_handler,
					   dtls_close_handler, alloc);
			if (err) {
				re_fprintf(stderr, "dtls_connect error: %m\n",
					   err);
				goto out;
			}
		}
		else {
			err = turnc_alloc(&alloc->turnc, NULL, IPPROTO_UDP,
					  alloc->us, TURN_LAYER, &alloc->srv,
					  alloc->user, alloc->pass,
					  TURN_DEFAULT_LIFETIME,
					  turnc_handler, alloc);
			if (err) {
				re_fprintf(stderr, "allocation: failed to"
					   " create TURN client"
					   " (%m)\n", err);
				goto out;
			}
		}
		break;

	case IPPROTO_TCP:
		err = tcp_connect(&alloc->tc, &alloc->srv, tcp_estab_handler,
				  tcp_recv_handler, tcp_close_handler, alloc);
		if (err)
			break;

		if (alloc->secure) {
			err = tls_start_tcp(&alloc->tlsc, alloc->tls, alloc->tc, 0);
			if (err)
				break;
		}
		break;

	default:
		err = EPROTONOSUPPORT;
		goto out;
	}

 out:
	return err;
}
