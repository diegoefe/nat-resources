#ifndef PJWRAP_H_INCLUIDO
#define PJWRAP_H_INCLUIDO

#include <stdlib.h>
#include <pjlib.h>
#include <pjlib-util.h>
#include <pjnath.h>


/* For this demo app, configure longer STUN keep-alive time
 * so that it does't clutter the screen output.
 */
#define KA_INTERVAL 300

enum State {
   Error=1, Start, IceDoneInit, IceDoneCheck
};

typedef struct
{
   pj_str_t  name;
   int log_level;
   int state;

	/* Command line options are stored here */
	struct options {
		unsigned    comp_cnt;
		pj_str_t    ns;
		int	    max_host;
		pj_bool_t   regular;
		pj_str_t    stun_srv;
		const char *log_file;
		pj_str_t rem_sdp;
      pj_bool_t write_sdp;
	} opt;

	/* Our global variables */
	pj_caching_pool	 cp;
	pj_pool_t		*pool;
	pj_thread_t		*thread;
	pj_bool_t		 thread_quit_flag;
	pj_ice_strans_cfg	 ice_cfg;
   pj_ice_strans_cb icecb;
	pj_ice_strans	*icest;
	FILE		*log_fhnd;

	/* Variables to store parsed remote ICE info */
	struct rem_info {
		char		 ufrag[80];
		char		 pwd[80];
		unsigned	 comp_cnt;
		pj_sockaddr	 def_addr[PJ_ICE_MAX_COMP];
		unsigned	 cand_cnt;
		pj_ice_sess_cand cand[PJ_ICE_ST_MAX_CAND];
	} rem;

} app_t;


pj_status_t app_init(app_t* _app);
void app_start(app_t* _app, char _role);
void app_show_ice(app_t* _app);
void app_start_nego(app_t* _app);
void app_stop(app_t* _app);
void err_exit(app_t* _app, const char *title, pj_status_t status);

typedef void (*f_on_ice_complete)(pj_ice_strans *ice_st, pj_ice_strans_op op, pj_status_t status);
typedef void (*f_on_ice_rx_data)(pj_ice_strans*, unsigned, void*, pj_size_t, const pj_sockaddr_t *, unsigned);
void ice_set_cbs(pj_ice_strans_cb* _iscb, f_on_ice_complete _complete_cb, f_on_ice_rx_data _data_cb);

#endif

