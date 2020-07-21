#include "turn_cfg.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <re.h>

int main() {
	int err = libre_init();
	if (err) {
		re_fprintf(stderr, "re init failed: %s\n", strerror(err));
		goto out;
	}

 out:
	//mem_deref(sdp);
	libre_close();

	mem_debug();
	tmr_debug();
	return err;
}
