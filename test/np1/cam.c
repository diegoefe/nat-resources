#include <stdio.h>

#include "pjwrap.h"
#include "np1.h"

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
		cam.state = IceDoneInit;
	} else {
		char errmsg[PJ_ERR_MSG_SIZE];
		pj_strerror(status, errmsg, sizeof(errmsg));
		PJ_LOG(1,(THIS_FILE, "ICE %s failed: %s", opname, errmsg));
		pj_ice_strans_destroy(ice_st);
		// GLOBAL GARBAGE (must go away!)
		cam.icest = NULL;
		cam.state = Error;
	}
}

/*
 * This is the callback that is registered to the ICE stream transport to
 * receive notification about incoming data. By "data" it means application
 * data such as RTP/RTCP, and not packets that belong to ICE signaling (such
 * as STUN connectivity checks or TURN signaling).
 */
void cb_on_rx_data(pj_ice_strans *ice_st,
			  unsigned comp_id, 
			  void *pkt, pj_size_t size,
			  const pj_sockaddr_t *src_addr,
			  unsigned src_addr_len)
{
    char ipstr[PJ_INET6_ADDRSTRLEN+10];

    PJ_UNUSED_ARG(ice_st);
    PJ_UNUSED_ARG(src_addr_len);
    PJ_UNUSED_ARG(pkt);

    // Don't do this! It will ruin the packet buffer in case TCP is used!
    //((char*)pkt)[size] = '\0';

    PJ_LOG(1,(THIS_FILE, "Component %d: received %d bytes data from %s: \"%.*s\"",
	      comp_id, size,
	      pj_sockaddr_print(src_addr, ipstr, sizeof(ipstr), 3),
	      (unsigned)size,
	      (char*)pkt));
}


/*
 * And here's the main()
 */
int main(int argc, char *argv[]) {
	struct pj_getopt_option long_options[] = {
		{ "help",		0, 0, 'h'},
		{ "stun-srv",	1, 0, 's'},
		{ "log-file",	1, 0, 'L'},
		{ "remote-sdp",	1, 0, 'R'},
		{ "write-sdp",	0, 0, 'W'},
	};
	int c, opt_id;
	pj_status_t status;

	cam.name = pj_str("cam");
	// cam.log_level = 3;
	cam.log_level = 1;
	cam.state = Start;
	cam.opt.comp_cnt = NUM_CANDIDATES;
	cam.opt.max_host = -1;
   cam.opt.rem_sdp.ptr = 0;
   cam.opt.rem_sdp.slen = 0;
   cam.opt.write_sdp = PJ_FALSE;

	while((c=pj_getopt_long(argc,argv, "s:h:L:R:W", long_options, &opt_id))!=-1) {
		switch (c) {
			case 'h':
				app_usage(&cam);
				return 0;
			case 's':
				cam.opt.stun_srv = pj_str(pj_optarg);
				break;
			case 'L':
				cam.opt.log_file = pj_optarg;
				break;
			case 'R':
				cam.opt.rem_sdp= pj_str(pj_optarg);
				break;
			case 'W':
            cam.opt.write_sdp = PJ_TRUE;
				break;
			default:
				printf("Argument \"%s\" is not valid. Use -h to see help\n",
				argv[pj_optind]);
				return 1;
		}
	}
   if(0 == cam.opt.rem_sdp.slen) {
      cam.opt.rem_sdp= pj_str((char*)default_remote_sdp(cam.name.ptr));
   }
   printf("SDP FILE(%s)\n", cam.opt.rem_sdp.ptr);

	status = app_init(&cam);
	if(status != PJ_SUCCESS) { return 1; }

	printf("starting...\n");
   ice_set_cbs(&cam.icecb, cb_on_ice_complete, cb_on_rx_data);
	app_start(&cam, 'o');
	while(cam.state != IceDoneInit) { pj_thread_sleep(100); }
	app_show_ice(&cam);
   if(PJ_FALSE == cam.opt.write_sdp) {
	  // app_input_remote();
	  // app_start_nego()
   }
	app_stop(&cam);
	err_exit(&cam, "Quitting..", PJ_SUCCESS);
	return 0;
}


