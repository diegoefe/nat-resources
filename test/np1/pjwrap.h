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

typedef struct
{
   pj_str_t  name;
   int log_level;

	/* Command line options are stored here */
	struct options {
		unsigned    comp_cnt;
		pj_str_t    ns;
		int	    max_host;
		pj_bool_t   regular;
		pj_str_t    stun_srv;
		const char *log_file;
	} opt;

	/* Our global variables */
	pj_caching_pool	 cp;
	pj_pool_t		*pool;
	pj_thread_t		*thread;
	pj_bool_t		 thread_quit_flag;
	pj_ice_strans_cfg	 ice_cfg;
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


void app_perror(const char *title, pj_status_t status);
void err_exit(app_t* _app, const char *title, pj_status_t status);
pj_status_t handle_events(app_t* _app, unsigned max_msec, unsigned *p_count);
/**/int app_worker_thread(void *unused);
void cb_on_rx_data(pj_ice_strans *ice_st, unsigned comp_id, void *pkt, pj_size_t size, const pj_sockaddr_t *src_addr, unsigned src_addr_len);
/**/void cb_on_ice_complete(pj_ice_strans *ice_st, pj_ice_strans_op op, pj_status_t status);
void log_func(app_t* _app, int level, const char *data, int len);
pj_status_t app_init(app_t* _app);
typedef void (*f_on_ice_complete)(pj_ice_strans *ice_st, 
			       pj_ice_strans_op op,
			       pj_status_t status);
void app_create_instance(app_t* _app, f_on_ice_complete _on_ice_complete);
void reset_rem_info(app_t* _app);
void app_destroy_instance(app_t* _app);
void app_init_session(app_t* _app, unsigned rolechar);
void app_stop_session(app_t* _app);
int print_cand(char buffer[], unsigned maxlen, const pj_ice_sess_cand *cand);
int encode_session(app_t* _app, char buffer[], unsigned maxlen);
void app_show_ice(app_t* _app);
void app_input_remote(app_t* _app);
void app_start_nego(app_t* _app);
void app_send_data(app_t* _app, unsigned comp_id, const char *data);
void app_help_menu(app_t* _app);
void app_print_menu(void);
void app_usage(app_t* _app);

void app_start(app_t* _app, f_on_ice_complete);
void app_stop(app_t* _app);

#endif

