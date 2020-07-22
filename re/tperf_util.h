#ifndef MY_TPERF_UTIL_H_INCLUIDO
#define MY_TPERF_UTIL_H_INCLUIDO

#include <stdint.h>
#include <re.h>

#define HDR_SIZE 20
#define PATTERN 0xa5

struct hdr {
	uint32_t session_cookie;
	uint32_t alloc_id;
	uint32_t seq;
	uint32_t payload_len;

	uint8_t payload[256];
};

typedef void (allocation_h)(int err, uint16_t scode, const char *reason,
			    const struct sa *srv,  const struct sa *relay,
			    void *arg);

struct allocator {
	struct list allocl;
	struct tmr tmr;
	struct tmr tmr_ui;
	unsigned num_allocations;
	unsigned num_sent;
	unsigned num_received;

	bool server_info;
	bool server_auth;
	char server_software[256];
	struct sa mapped_addr;
	uint32_t lifetime;

	uint64_t tick, tock;
	uint32_t session_cookie;
	time_t traf_start_time;

	struct tmr tmr_pace;
};

struct sender {
	struct allocation *alloc;  /* pointer */
	uint32_t session_cookie;
	uint32_t alloc_id;
	uint32_t seq;

	unsigned bitrate;          /* target bitrate [bit/s] */
	unsigned ptime;
	size_t psize;

	uint64_t ts;               /* running timestamp */
	uint64_t ts_start;
	uint64_t ts_stop;

	uint64_t total_bytes;
	uint64_t total_packets;
};

struct receiver {
	uint32_t cookie;
	uint32_t allocid;
	uint64_t ts_start;
	uint64_t ts_last;
	uint64_t total_bytes;
	uint64_t total_packets;
	uint32_t last_seq;
};

struct allocation {
	struct le le;
	struct allocator *allocator;  /* pointer to container */
	struct udp_sock *us;
	struct turnc *turnc;
	struct timeval sent;
	int proto;
	bool secure;
	struct sa srv;
	const char *user;
	const char *pass;
	struct sa relay;
	struct sa peer;
	struct tcp_conn *tc;
	struct tls_conn *tlsc;
	struct tls *tls;
	struct dtls_sock *dtls_sock;
	struct mbuf *mb;              /* TCP re-assembly buffer */
	struct sender *sender;
	struct receiver recv;
	struct udp_sock *us_tx;
	struct sa laddr_tx;
	struct tmr tmr_ping;
	double atime;                 /* ms */
	unsigned ix;
	bool ok;
	bool turn_ind;
	unsigned redirc;
	int err;
	allocation_h *alloch;
	void *arg;
};

int dns_init(struct dnsc **dnsc);
const char *protocol_name(int proto, bool secure);
void allocator_stop_senders(struct allocator *allocator);
int start(struct allocation *alloc);
int allocation_create(struct allocator *allocator, unsigned ix, int proto,
		      const struct sa *srv,
		      const char *username, const char *password,
		      struct tls *tls, bool turn_ind,
		      allocation_h *alloch, void *arg);

void destructor(void *arg);
#endif
