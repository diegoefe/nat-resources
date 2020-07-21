#include "util.h"

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
