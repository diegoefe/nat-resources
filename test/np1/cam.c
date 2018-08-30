#include <stdio.h>

#include "pjwrap.h"

app_t cam;

#define THIS_FILE   "cam.c"

/*
 * This is the callback that is registered to the ICE stream transport to
 * receive notification about ICE state progression.
 */
void cb_on_ice_complete(pj_ice_strans *ice_st, 
			       pj_ice_strans_op op,
			       pj_status_t status)
{
    const char *opname = (op==PJ_ICE_STRANS_OP_INIT? "initialization" :
	    (op==PJ_ICE_STRANS_OP_NEGOTIATION ? "negotiation" : "unknown_op"));

	if (status == PJ_SUCCESS) {
		PJ_LOG(1,(THIS_FILE, "ICE %s successful", opname));
	} else {
		char errmsg[PJ_ERR_MSG_SIZE];
		pj_strerror(status, errmsg, sizeof(errmsg));
		PJ_LOG(1,(THIS_FILE, "ICE %s failed: %s", opname, errmsg));
		pj_ice_strans_destroy(ice_st);
		// GLOBAL GARBAGE (must go away!)
		cam.icest = NULL;
	}
}

/*
 * And here's the main()
 */
int main(int argc, char *argv[])
{
	memset(&cam, 0, sizeof(cam));

    struct pj_getopt_option long_options[] = {
	{ "help",		0, 0, 'h'},
	{ "stun-srv",		1, 0, 's'},
	{ "log-file",		1, 0, 'L'},
    };
    int c, opt_id;
    pj_status_t status;

    cam.opt.comp_cnt = 1;
    cam.opt.max_host = -1;

    while((c=pj_getopt_long(argc,argv, "s:h:L", long_options, &opt_id))!=-1) {
		switch (c) {
			case 'h':
				app_usage();
				return 0;
			case 's':
				cam.opt.stun_srv = pj_str(pj_optarg);
				break;
			case 'L':
				cam.opt.log_file = pj_optarg;
				break;
			default:
				printf("Argument \"%s\" is not valid. Use -h to see help",
				argv[pj_optind]);
				return 1;
		}
    }

    status = app_init(&cam);
    if (status != PJ_SUCCESS)
	return 1;

    // app_console();
	 app_start(&cam, cb_on_ice_complete);
	getchar();
	app_stop(&cam);

    err_exit(&cam, "Quitting..", PJ_SUCCESS);
    return 0;
}

