#include "turn_cfg.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <re.h>

int main() {
	int err = libre_init();
	if(err) {
		re_fprintf(stderr, "re init failed: %s\n", strerror(err));
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
	libre_close();

	mem_debug();
	tmr_debug();
	return err;
}
