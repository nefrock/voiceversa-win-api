// This is the main DLL file.

#include "stdafx.h"
#include "vvAPICpp.h"

#include <pjsua-lib/pjsua.h>


#define THIS_FILE	"vvAPICpp.cpp"
#define NO_LIMIT	(int)0x7FFFFFFF

/* Ringtones */
#define RINGBACK_FREQ1	  440
#define RINGBACK_FREQ2	  480
#define RINGBACK_ON	      2000
#define RINGBACK_OFF	  4000
#define RINGBACK_CNT	  1
#define RINGBACK_INTERVAL 4000

#define RING_FREQ1	    800
#define RING_FREQ2	    640
#define RING_ON		    300
#define RING_OFF	    100
#define RING_CNT	    3
#define RING_INTERVAL	2000

// global function pointers
static fptr_regstate* cb_regstate = 0;
static fptr_callstate* cb_callstate = 0;
static fptr_callincoming* cb_callincoming = 0;
static fptr_getconfigdata* cb_getconfigdata = 0;
static fptr_callholdconf* cb_callholdconf = 0;
static fptr_callretrieveconf* cb_callretrieveconf = 0;
static fptr_buddystatus* cb_buddystatus = 0;
static fptr_msgrec* cb_messagereceived = 0;
static fptr_dtmfdigit* cb_dtmfdigit = 0;
static fptr_mwi* cb_mwi = 0;
static fptr_crep* cb_crep = 0;

enum {
    SC_Deflect,
    SC_CFU,
    SC_CFNR,
    SC_DND,
    SC_3Pty,
	SC_CFB
};

enum ETransportMode {
	TM_UDP,
	TM_TCP,
	TM_TLS
};

// sipek configuration container
static SipConfigStruct sipek_config;
static bool sipekConfigEnabled = false;

////////////////////////////////////////////////////////////////////////
// Presence structs 

enum {
	AVAILABLE, BUSY, OTP, IDLE, AWAY, BRB, OFFLINE, OPT_MAX
};

struct presence_status {
	int id;
	char *name;
} opts[] = {
		{ AVAILABLE, "Available" },
		{ BUSY, "Busy"},
		{ OTP, "On the phone"},
		{ IDLE, "Idle"},
		{ AWAY, "Away"},
		{ BRB, "Be right back"},
		{ OFFLINE, "Offline"}
    };

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

/* Call specific data */
struct call_data
{
	pj_timer_entry	    timer;
    pj_bool_t		    ringback_on;
    pj_bool_t		    ring_on;
};


/* Pjsua application data */
static struct app_config
{
	pjsua_config	    cfg;
	pjsua_logging_config    log_cfg;
	pjsua_media_config	    media_cfg;
	pj_bool_t		    no_refersub;
	pj_bool_t		    no_tcp;
	pj_bool_t		    no_udp;
	pj_bool_t		    use_tls;
	pjsua_transport_config  udp_cfg;
	pjsua_transport_config  rtp_cfg;

	unsigned		    acc_cnt;
	pjsua_acc_config	acc_cfg[PJSUA_MAX_ACC];

	unsigned		    buddy_cnt;
	pjsua_buddy_config	buddy_cfg[PJSUA_MAX_BUDDIES];

	struct call_data	call_data[PJSUA_MAX_CALLS];

	pj_pool_t		   *pool;
	/* Compatibility with older pjsua */

	unsigned		    codec_cnt;
	pj_str_t		    codec_arg[32];
    unsigned		    codec_dis_cnt;
    pj_str_t            codec_dis[32];
	pj_bool_t		    null_audio;
	unsigned		    wav_count;
	pj_str_t		    wav_files[32];
	unsigned		    tone_count;
	pjmedia_tone_desc	tones[32];
	pjsua_conf_port_id	tone_slots[32];
	pjsua_player_id	    wav_id;
	pjsua_conf_port_id	wav_port;
	pj_bool_t		    auto_play;
    pj_bool_t		    auto_play_hangup;
    pj_timer_entry	    auto_hangup_timer;
	pj_bool_t		    auto_loop;
	pj_bool_t		    auto_conf;
	pj_str_t		    rec_file;
	pj_bool_t		    auto_rec;
	pjsua_recorder_id	rec_id;
	pjsua_conf_port_id	rec_port;
	unsigned		    auto_answer;
	unsigned		    duration;

#ifdef STEREO_DEMO
    pjmedia_snd_port	   *snd;
#endif

	float		    mic_level, speaker_level;

    int			    capture_dev, playback_dev;
    unsigned		capture_lat, playback_lat;

    pj_bool_t		no_tones;
    int			    ringback_slot;
    int			    ringback_cnt;
    pjmedia_port	*ringback_port;
    int			    ring_slot;
    int			    ring_cnt;
    pjmedia_port	*ring_port;

} app_config;


//static pjsua_acc_id	current_acc;
#define current_acc	pjsua_acc_get_default()
static pjsua_call_id	current_call = PJSUA_INVALID_ID;
static pj_bool_t	cmd_echo;
static int		stdout_refresh = -1;
static const char      *stdout_refresh_text = "STDOUT_REFRESH";
static pj_bool_t	stdout_refresh_quit = PJ_FALSE;
static pj_str_t		uri_arg;

static char some_buf[1024 * 3];

#ifdef STEREO_DEMO
static void stereo_demo();
#endif
pj_status_t app_destroy(void);

static void ringback_start(pjsua_call_id call_id);
static void ring_start(pjsua_call_id call_id);
static void ring_stop(pjsua_call_id call_id);



//////////////////////////////////////////////////////////////////////////
// Request handler to receive out-of-dialog NOTIFY (from Asterisk)
static pj_bool_t on_rx_request(pjsip_rx_data *rdata)
{
	if (strstr(pj_strbuf(&rdata->msg_info.msg->line.req.method.name),
		"NOTIFY"))
	{
		pjsip_generic_string_hdr * hdr;
		pj_str_t did_str = pj_str("Event");
		hdr = (pjsip_generic_string_hdr*) pjsip_msg_find_hdr_by_name(rdata->msg_info.msg, &did_str, NULL);
		if (!hdr) return false;

		// We have an event header, now determine if it's contents are "message-summary"
		if (pj_strcmp2(&hdr->hvalue, "message-summary")) return false;

		pjsip_msg_body * body_p = rdata->msg_info.msg->body;

		char* buf = (char*)pj_pool_alloc(app_config.pool, body_p->len);
		memcpy(buf, body_p->data, body_p->len);

		// Process body message as desired...
		if (strstr(buf, "Messages-Waiting: yes") != 0)
		{
			if (cb_mwi != 0) cb_mwi(1, buf);
		}
		else
		{
			if (cb_mwi != 0) cb_mwi(0, buf);
		}
		PJ_LOG(3,(THIS_FILE,"MWI message: %s", buf));
	}

	pjsip_endpt_respond_stateless(pjsip_ua_get_endpt(pjsip_ua_instance()),
		rdata, 200, NULL,
		NULL, NULL);

	return PJ_TRUE;
}


//////////////////////////////////////////////////////////////////////////

/* Set default config. */
static void default_config(struct app_config *cfg)
{
	char tmp[80];
    unsigned i;

	pjsua_config_default(&cfg->cfg);
	pj_ansi_sprintf(tmp, "Sipek on PJSUA v%s", pj_get_version());
	pj_strdup2_with_null(app_config.pool, &cfg->cfg.user_agent, tmp);

	pjsua_logging_config_default(&cfg->log_cfg);
	pjsua_media_config_default(&cfg->media_cfg);
	pjsua_transport_config_default(&cfg->udp_cfg);
	cfg->udp_cfg.port = 5060;
	pjsua_transport_config_default(&cfg->rtp_cfg);
	cfg->rtp_cfg.port = 4000;
	cfg->duration = NO_LIMIT;
	cfg->wav_id = PJSUA_INVALID_ID;
	cfg->rec_id = PJSUA_INVALID_ID;
	cfg->wav_port = PJSUA_INVALID_ID;
	cfg->rec_port = PJSUA_INVALID_ID;
	cfg->mic_level = cfg->speaker_level = 1.0;
    cfg->capture_dev = PJSUA_INVALID_ID;
    cfg->playback_dev = PJSUA_INVALID_ID;
    cfg->capture_lat = PJMEDIA_SND_DEFAULT_REC_LATENCY;
    cfg->playback_lat = PJMEDIA_SND_DEFAULT_PLAY_LATENCY;
    cfg->ringback_slot = PJSUA_INVALID_ID;
    cfg->ring_slot = PJSUA_INVALID_ID;

    for (i=0; i<PJ_ARRAY_SIZE(cfg->acc_cfg); ++i) {
		pjsua_acc_config_default(&cfg->acc_cfg[i]);
	}

    for (i=0; i<PJ_ARRAY_SIZE(cfg->buddy_cfg); ++i) {
		pjsua_buddy_config_default(&cfg->buddy_cfg[i]);
	}

	cfg->log_cfg.log_filename = pj_str("pjsip.log");
}

/*
 * Find next call when current call is disconnected or when user
 * press ']'
 */
static pj_bool_t find_next_call(void)
{
    int i, max;

    max = pjsua_call_get_max_count();
    for (i=current_call+1; i<max; ++i) {
	if (pjsua_call_is_active(i)) {
	    current_call = i;
	    return PJ_TRUE;
	}
    }

    for (i=0; i<current_call; ++i) {
	if (pjsua_call_is_active(i)) {
	    current_call = i;
	    return PJ_TRUE;
	}
    }

    current_call = PJSUA_INVALID_ID;
    return PJ_FALSE;
}


/*
 * Print log of call states. Since call states may be too long for logger,
 * printing it is a bit tricky, it should be printed part by part as long 
 * as the logger can accept.
 */
static void log_call_dump(int call_id) {
    pj_size_t call_dump_len;
    pj_size_t part_len;
    pj_size_t part_idx;
    unsigned log_decor;

    pjsua_call_dump(call_id, PJ_TRUE, some_buf, 
		    sizeof(some_buf), "  ");
    call_dump_len = strlen(some_buf);

    log_decor = pj_log_get_decor();
    pj_log_set_decor(log_decor & ~(PJ_LOG_HAS_NEWLINE | PJ_LOG_HAS_CR));
    PJ_LOG(3,(THIS_FILE, "\n"));
    pj_log_set_decor(0);

    part_idx = 0;
    part_len = PJ_LOG_MAX_SIZE-80;
    while (part_idx < call_dump_len) {
	char p_orig, *p;

	p = &some_buf[part_idx];
	if (part_idx + part_len > call_dump_len)
	    part_len = call_dump_len - part_idx;
	p_orig = p[part_len];
	p[part_len] = '\0';
	PJ_LOG(3,(THIS_FILE, "%s", p));
	p[part_len] = p_orig;
	part_idx += part_len;
    }
    pj_log_set_decor(log_decor);
}


//////////////////////////////////////////////////////////////////////////

PJSIPDLL_DLL_API int onRegStateCallback(fptr_regstate cb)
{
	cb_regstate = cb;
	return 1;
}
 
PJSIPDLL_DLL_API int onCallStateCallback(fptr_callstate cb)
{
	cb_callstate = cb;
	return 1;
}

PJSIPDLL_DLL_API int onCallIncoming(fptr_callincoming cb)
{
	cb_callincoming = cb;
	return 1;
}

PJSIPDLL_DLL_API int getConfigDataCallback(fptr_getconfigdata cb)
{
	cb_getconfigdata = cb;
	return 1;
}

PJSIPDLL_DLL_API int onCallHoldConfirmCallback(fptr_callholdconf cb)
{
  cb_callholdconf = cb;
	return 1;
}

PJSIPDLL_DLL_API int onMessageReceivedCallback(fptr_msgrec cb)
{
  cb_messagereceived = cb;
  return 1;
}

PJSIPDLL_DLL_API int onBuddyStatusChangedCallback(fptr_buddystatus cb)
{
  cb_buddystatus = cb;
  return 1;
}

PJSIPDLL_DLL_API int onDtmfDigitCallback(fptr_dtmfdigit cb)
{
  cb_dtmfdigit = cb;
  return 1;
}

PJSIPDLL_DLL_API int onMessageWaitingCallback(fptr_mwi cb)
{
	cb_mwi = cb;
	return 1;
}

PJSIPDLL_DLL_API int onCallReplaced(fptr_crep cb)
{
	cb_crep = cb;
	return 1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////
// Callbacks
//////////////////////////////////////////////////////////////////////////

static void ringback_start(pjsua_call_id call_id)
{
	if (app_config.no_tones) { return; }

	if (app_config.call_data[call_id].ringback_on) { return; }

    app_config.call_data[call_id].ringback_on = PJ_TRUE;

    if (++app_config.ringback_cnt == 1 && 
			app_config.ringback_slot != PJSUA_INVALID_ID) {
		pjsua_conf_connect(app_config.ringback_slot, 0);
    }
}

static void ring_start(pjsua_call_id call_id)
{
	if (app_config.no_tones) { return; }

	if (app_config.call_data[call_id].ring_on) { return; }

    app_config.call_data[call_id].ring_on = PJ_TRUE;

    if (++app_config.ring_cnt == 1 && 
			app_config.ring_slot != PJSUA_INVALID_ID) {
		pjsua_conf_connect(app_config.ring_slot, 0);
    }
}

static void ring_stop(pjsua_call_id call_id)
{
	if (app_config.no_tones) { return; }

    if (app_config.call_data[call_id].ringback_on) {
		app_config.call_data[call_id].ringback_on = PJ_FALSE;

		pj_assert(app_config.ringback_cnt > 0);
		if (--app_config.ringback_cnt == 0 && 
				app_config.ringback_slot != PJSUA_INVALID_ID) {
			pjsua_conf_disconnect(app_config.ringback_slot, 0);
			pjmedia_tonegen_rewind(app_config.ringback_port);
		}
    }

    if (app_config.call_data[call_id].ring_on) {
		app_config.call_data[call_id].ring_on = PJ_FALSE;

		pj_assert(app_config.ring_cnt > 0);
		if (--app_config.ring_cnt == 0 && 
				app_config.ring_slot != PJSUA_INVALID_ID) {
			pjsua_conf_disconnect(app_config.ring_slot, 0);
			pjmedia_tonegen_rewind(app_config.ring_port);
		}
    }
}

/* Callback from timer when the maximum call duration has been
 * exceeded.
 */
static void call_timeout_callback(pj_timer_heap_t *timer_heap,
				  struct pj_timer_entry *entry)
{
    pjsua_call_id call_id = entry->id;
    pjsua_msg_data msg_data;
    pjsip_generic_string_hdr warn;
    pj_str_t hname = pj_str("Warning");
    pj_str_t hvalue = pj_str("399 pjsua \"Call duration exceeded\"");

    PJ_UNUSED_ARG(timer_heap);

    if (call_id == PJSUA_INVALID_ID) {
	PJ_LOG(1,(THIS_FILE, "Invalid call ID in timer callback"));
	return;
    }
    
    /* Add warning header */
    pjsua_msg_data_init(&msg_data);
    pjsip_generic_string_hdr_init2(&warn, &hname, &hvalue);
    pj_list_push_back(&msg_data.hdr_list, &warn);

    /* Call duration has been exceeded; disconnect the call */
    PJ_LOG(3,(THIS_FILE, "Duration (%d seconds) has been exceeded "
			 "for call %d, disconnecting the call",
			 app_config.duration, call_id));
    entry->id = PJSUA_INVALID_ID;
    pjsua_call_hangup(call_id, 200, NULL, &msg_data);
}


/*
 * Handler when invite state has changed.
 */
static void on_call_state(pjsua_call_id call_id, pjsip_event *e)
{
	pjsua_call_info call_info;

	PJ_UNUSED_ARG(e);

	pjsua_call_get_info(call_id, &call_info);

	if (call_info.state == PJSIP_INV_STATE_DISCONNECTED) {
		/* Stop all ringback for this call */
		ring_stop(call_id);

		/* Cancel duration timer, if any */
		if (app_config.call_data[call_id].timer.id != PJSUA_INVALID_ID) {
			struct call_data *cd = &app_config.call_data[call_id];
			pjsip_endpoint *endpt = pjsua_get_pjsip_endpt();

			cd->timer.id = PJSUA_INVALID_ID;
			pjsip_endpt_cancel_timer(endpt, &cd->timer);
		}

		/* Rewind play file when hangup automatically, 
		 * since file is not looped
		 */
		if (app_config.auto_play_hangup) {
			pjsua_player_set_pos(app_config.wav_id, 0);
		}

		PJ_LOG(3,(THIS_FILE, "Call %d is DISCONNECTED [reason=%d (%s)]", 
				call_id,
				call_info.last_status,
				call_info.last_status_text.ptr));

		if (call_id == current_call) {
			find_next_call();
		}

		/* Dump media state upon disconnected */
		if (1) {
			PJ_LOG(5,(THIS_FILE, "Call %d disconnected, dumping media stats..", call_id));
			log_call_dump(call_id);
		}

	} else {

		if (app_config.duration != NO_LIMIT && 
				call_info.state == PJSIP_INV_STATE_CONFIRMED) {
			/* Schedule timer to hangup call after the specified duration */
			struct call_data *cd = &app_config.call_data[call_id];
			pjsip_endpoint *endpt = pjsua_get_pjsip_endpt();
			pj_time_val delay;

			cd->timer.id = call_id;
			delay.sec = app_config.duration;
			delay.msec = 0;
			pjsip_endpt_schedule_timer(endpt, &cd->timer, &delay);
		}

		if (call_info.state == PJSIP_INV_STATE_EARLY) {
			int code;
			pj_str_t reason;
			pjsip_msg *msg;

			/* This can only occur because of TX or RX message */
			pj_assert(e->type == PJSIP_EVENT_TSX_STATE);

			if (e->body.tsx_state.type == PJSIP_EVENT_RX_MSG) {
				msg = e->body.tsx_state.src.rdata->msg_info.msg;
			} else {
				msg = e->body.tsx_state.src.tdata->msg;
			}

			code = msg->line.status.code;
			reason = msg->line.status.reason;

			/* Start ringback for 180 for UAC unless there's SDP in 180 */
			if (call_info.role == PJSIP_ROLE_UAC && code == 180 && 
					msg->body == NULL && 
					call_info.media_status == PJSUA_CALL_MEDIA_NONE) {
				ringback_start(call_id);
			}

			PJ_LOG(3,(THIS_FILE, "Call %d state changed to %s (%d %.*s)", 
					call_id, call_info.state_text.ptr,
					code, (int)reason.slen, reason.ptr));
		} else {
			PJ_LOG(3,(THIS_FILE, "Call %d state changed to %s", 
					call_id,
					call_info.state_text.ptr));
		}

		if (current_call==PJSUA_INVALID_ID) {
			current_call = call_id;
		}
	}

	// callback
	if (cb_callstate != 0) cb_callstate(call_id, call_info.state);
}


/**
 * Handler when there is incoming call.
 */
static void on_incoming_call(pjsua_acc_id acc_id, pjsua_call_id call_id,
														 pjsip_rx_data *rdata)
{
	pjsua_call_info call_info;

    PJ_UNUSED_ARG(acc_id);
    PJ_UNUSED_ARG(rdata);	

	ring_start(call_id);
	pjsua_call_get_info(call_id, &call_info);

	if (cb_callincoming != 0) cb_callincoming(call_id, call_info.remote_info.ptr);
}


/*
 * Handler when a transaction within a call has changed state.
 */
static void on_call_tsx_state(pjsua_call_id call_id,
			      pjsip_transaction *tsx,
			      pjsip_event *e)
{
    const pjsip_method info_method = 
    {
	PJSIP_OTHER_METHOD,
	{ "INFO", 4 }
    };

    if (pjsip_method_cmp(&tsx->method, &info_method)==0) {
	/*
	 * Handle INFO method.
	 */
	if (tsx->role == PJSIP_ROLE_UAC && 
	    (tsx->state == PJSIP_TSX_STATE_COMPLETED ||
	       (tsx->state == PJSIP_TSX_STATE_TERMINATED &&
	        e->body.tsx_state.prev_state != PJSIP_TSX_STATE_COMPLETED))) 
	{
	    /* Status of outgoing INFO request */
	    if (tsx->status_code >= 200 && tsx->status_code < 300) {
		PJ_LOG(4,(THIS_FILE, 
			  "Call %d: DTMF sent successfully with INFO",
			  call_id));
	    } else if (tsx->status_code >= 300) {
		PJ_LOG(4,(THIS_FILE, 
			  "Call %d: Failed to send DTMF with INFO: %d/%.*s",
			  call_id,
		          tsx->status_code,
			  (int)tsx->status_text.slen,
			  tsx->status_text.ptr));
	    }
	} else if (tsx->role == PJSIP_ROLE_UAS &&
		   tsx->state == PJSIP_TSX_STATE_TRYING)
	{
	    /* Answer incoming INFO with 200/OK */
	    pjsip_rx_data *rdata;
	    pjsip_tx_data *tdata;
	    pj_status_t status;

	    rdata = e->body.tsx_state.src.rdata;

	    if (rdata->msg_info.msg->body) {
		status = pjsip_endpt_create_response(tsx->endpt, rdata,
						     200, NULL, &tdata);
		if (status == PJ_SUCCESS)
		    status = pjsip_tsx_send_msg(tsx, tdata);

		PJ_LOG(3,(THIS_FILE, "Call %d: incoming INFO:\n%.*s", 
			  call_id,
			  (int)rdata->msg_info.msg->body->len,
			  rdata->msg_info.msg->body->data));
	    } else {
		status = pjsip_endpt_create_response(tsx->endpt, rdata,
						     400, NULL, &tdata);
		if (status == PJ_SUCCESS)
		    status = pjsip_tsx_send_msg(tsx, tdata);
	    }
	}
    }
}

/* General processing for media state. "mi" is the media index */
static void on_call_generic_media_state(pjsua_call_info *ci, unsigned mi,
                                        pj_bool_t *has_error)
{
    const char *status_name[] = {
        "None",
        "Active",
        "Local hold",
        "Remote hold",
        "Error"
    };

    PJ_UNUSED_ARG(has_error);

    pj_assert(ci->media[mi].status <= PJ_ARRAY_SIZE(status_name));
    pj_assert(PJSUA_CALL_MEDIA_ERROR == 4);

	/*
    PJ_LOG(4,(THIS_FILE, "Call %d media %d [type=%s], status is %s",
	      ci->id, mi, pjmedia_type_name(ci->media[mi].type),
	      status_name[ci->media[mi].status]));
	 */
	PJ_LOG(4,(THIS_FILE, "Call %d media %d [type=%d], status is %s",
	      ci->id, mi, ci->media[mi].type,
	      status_name[ci->media[mi].status]));
}

/* Process audio media state. "mi" is the media index. */
static void on_call_audio_state(pjsua_call_info *ci, unsigned mi,
                                pj_bool_t *has_error)
{
    PJ_UNUSED_ARG(has_error);

    /* Stop ringback */
    ring_stop(ci->id);

    /* Connect ports appropriately when media status is ACTIVE or REMOTE HOLD,
     * otherwise we should NOT connect the ports.
     */
    if (ci->media[mi].status == PJSUA_CALL_MEDIA_ACTIVE ||
			ci->media[mi].status == PJSUA_CALL_MEDIA_REMOTE_HOLD) {
		pj_bool_t connect_sound = PJ_TRUE;
		pjsua_conf_port_id call_conf_slot;

		call_conf_slot = ci->media[mi].stream.aud.conf_slot;

		/* Loopback sound, if desired */
		if (app_config.auto_loop) {
			pjsua_conf_connect(call_conf_slot, call_conf_slot);
			connect_sound = PJ_FALSE;
		}

		/* Automatically record conversation, if desired */
		if (app_config.auto_rec && app_config.rec_port != PJSUA_INVALID_ID) {
			pjsua_conf_connect(call_conf_slot, app_config.rec_port);
		}

		/* Stream a file, if desired */
		if ((app_config.auto_play || app_config.auto_play_hangup) && 
				app_config.wav_port != PJSUA_INVALID_ID) {
			pjsua_conf_connect(app_config.wav_port, call_conf_slot);
			connect_sound = PJ_FALSE;
		}

		/* Put call in conference with other calls, if desired */
		if (app_config.auto_conf) {
			pjsua_call_id call_ids[PJSUA_MAX_CALLS];
			unsigned call_cnt=PJ_ARRAY_SIZE(call_ids);
			unsigned i;

			/* Get all calls, and establish media connection between
			 * this call and other calls.
			 */
			pjsua_enum_calls(call_ids, &call_cnt);

			for (i=0; i<call_cnt; ++i) {
				if (call_ids[i] == ci->id)
					continue;
		
				if (!pjsua_call_has_media(call_ids[i]))
					continue;

				pjsua_conf_connect(call_conf_slot,
						   pjsua_call_get_conf_port(call_ids[i]));
				pjsua_conf_connect(pjsua_call_get_conf_port(call_ids[i]),
								   call_conf_slot);

				/* Automatically record conversation, if desired */
				if (app_config.auto_rec && app_config.rec_port != PJSUA_INVALID_ID) {
					pjsua_conf_connect(pjsua_call_get_conf_port(call_ids[i]), 
							   app_config.rec_port);
				}
			}

			/* Also connect call to local sound device */
			connect_sound = PJ_TRUE;
		}

		/* Otherwise connect to sound device */
		if (connect_sound) {
			pjsua_conf_connect(call_conf_slot, 0);
			pjsua_conf_connect(0, call_conf_slot);

			/* Automatically record conversation, if desired */
			if (app_config.auto_rec && app_config.rec_port != PJSUA_INVALID_ID) {
				pjsua_conf_connect(call_conf_slot, app_config.rec_port);
				pjsua_conf_connect(0, app_config.rec_port);
			}
		}
    }
}

/*
* Callback on media state changed event.
* The action may connect the call to sound device, to file, or
* to loop the call.
*/
static void on_call_media_state(pjsua_call_id call_id)
{
    pjsua_call_info call_info;
    unsigned mi;
    pj_bool_t has_error = PJ_FALSE;

    pjsua_call_get_info(call_id, &call_info);

    for (mi=0; mi<call_info.media_cnt; ++mi) {
		on_call_generic_media_state(&call_info, mi, &has_error);

		switch (call_info.media[mi].type) {
		case PJMEDIA_TYPE_AUDIO:
			on_call_audio_state(&call_info, mi, &has_error);
			break;
		case PJMEDIA_TYPE_VIDEO:
			//no videos for this app
			//on_call_video_state(&call_info, mi, &has_error);
			break;
		default:
			/* Make gcc happy about enum not handled by switch/case */
			break;
		}
    }

    if (has_error) {
		pj_str_t reason = pj_str("Media failed");
		pjsua_call_hangup(call_id, 500, &reason, NULL);
    }
}

/*
 * DTMF callback.
 */
static void call_on_dtmf_callback(pjsua_call_id call_id, int dtmf)
{
    PJ_LOG(3,(THIS_FILE, "Incoming DTMF on call %d: %c", call_id, dtmf));

    if (cb_dtmfdigit != 0) (*cb_dtmfdigit)(call_id, dtmf);
}

/*
 * Handler registration status has changed.
 */
static void on_reg_state(pjsua_acc_id acc_id)
{
    PJ_UNUSED_ARG(acc_id);

  pjsua_acc_info accinfo;

  pjsua_acc_get_info(acc_id, &accinfo);
  
	// callback
  if ((accinfo.status == 200)&&(accinfo.expires == -1))
         cb_regstate(acc_id, -1);
  else
         cb_regstate(acc_id, accinfo.status);
         
}

/*
 * Handler on buddy state changed.
 */
static void on_buddy_state(pjsua_buddy_id buddy_id)
{
    pjsua_buddy_info info;
    pjsua_buddy_get_info(buddy_id, &info);

    PJ_LOG(3,(THIS_FILE, "%.*s status is %.*s",
	      (int)info.uri.slen,
	      info.uri.ptr,
	      (int)info.status_text.slen,
	      info.status_text.ptr));

		char text[255] = {0};
		strncpy(text, info.status_text.ptr, (info.status_text.slen < 255) ? info.status_text.slen : 255);
	// callback
  if (cb_buddystatus != 0) cb_buddystatus(buddy_id, info.status, text);
}


/**
 * Incoming IM message (i.e. MESSAGE request)!
 */
static void on_pager(pjsua_call_id call_id, const pj_str_t *from, 
		     const pj_str_t *to, const pj_str_t *contact,
		     const pj_str_t *mime_type, const pj_str_t *text)
{
    /* Note: call index may be -1 */
    PJ_UNUSED_ARG(call_id);
    PJ_UNUSED_ARG(to);
    PJ_UNUSED_ARG(contact);
    PJ_UNUSED_ARG(mime_type);

    PJ_LOG(3,(THIS_FILE,"MESSAGE from %.*s: %.*s (%.*s)",
	      (int)from->slen, from->ptr,
	      (int)text->slen, text->ptr,
	      (int)mime_type->slen, mime_type->ptr)); 

   if (cb_messagereceived != 0) (*cb_messagereceived)(from->ptr, text->ptr);
}


/**
 * Received typing indication
 */
static void on_typing(pjsua_call_id call_id, const pj_str_t *from,
		      const pj_str_t *to, const pj_str_t *contact,
		      pj_bool_t is_typing)
{
    PJ_UNUSED_ARG(call_id);
    PJ_UNUSED_ARG(to);
    PJ_UNUSED_ARG(contact);

    PJ_LOG(3,(THIS_FILE, "IM indication: %.*s %s",
	      (int)from->slen, from->ptr,
	      (is_typing?"is typing..":"has stopped typing")));
}


/**
 * Call transfer request status.
 */
static void on_call_transfer_status(pjsua_call_id call_id,
				    int status_code,
				    const pj_str_t *status_text,
				    pj_bool_t final,
				    pj_bool_t *p_cont)
{
    PJ_LOG(3,(THIS_FILE, "Call %d: transfer status=%d (%.*s) %s",
	      call_id, status_code,
	      (int)status_text->slen, status_text->ptr,
	      (final ? "[final]" : "")));

    if (status_code/100 == 2) {
	PJ_LOG(3,(THIS_FILE, 
	          "Call %d: call transfered successfully, disconnecting call",
		  call_id));
	pjsua_call_hangup(call_id, PJSIP_SC_GONE, NULL, NULL);
	*p_cont = PJ_FALSE;
    }
}


/*
 * Notification that call is being replaced.
 */
static void on_call_replaced(pjsua_call_id old_call_id,
			     pjsua_call_id new_call_id)
{
    pjsua_call_info old_ci, new_ci;

    pjsua_call_get_info(old_call_id, &old_ci);
    pjsua_call_get_info(new_call_id, &new_ci);

    PJ_LOG(3,(THIS_FILE, "Call %d with %.*s is being replaced by "
			 "call %d with %.*s",
			 old_call_id, 
			 (int)old_ci.remote_info.slen, old_ci.remote_info.ptr,
			 new_call_id,
			 (int)new_ci.remote_info.slen, new_ci.remote_info.ptr));

		if (cb_crep != 0) (*cb_crep)(old_call_id, new_call_id);
}


/*
 * NAT type detection callback.
 */
static void on_nat_detect(const pj_stun_nat_detect_result *res)
{
    if (res->status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "NAT detection failed", res->status);
    } else {
	PJ_LOG(3, (THIS_FILE, "NAT detected as %s", res->nat_type_name));
    }
}

/*
 * Transport status notification
 */
static void on_transport_state(pjsip_transport *tp, 
			       pjsip_transport_state state,
			       const pjsip_transport_state_info *info)
{
    char host_port[128];

    pj_ansi_snprintf(host_port, sizeof(host_port), "[%.*s:%d]",
		     (int)tp->remote_name.host.slen,
		     tp->remote_name.host.ptr,
		     tp->remote_name.port);

    switch (state) {
    case PJSIP_TP_STATE_CONNECTED:
	{
	    PJ_LOG(3,(THIS_FILE, "SIP %s transport is connected to %s",
		     tp->type_name, host_port));
	}
	break;

    case PJSIP_TP_STATE_DISCONNECTED:
	{
	    char buf[100];

	    snprintf(buf, sizeof(buf), "SIP %s transport is disconnected "
		    "from %s", tp->type_name, host_port);
	    pjsua_perror(THIS_FILE, buf, info->status);
	}
	break;

    default:
	break;
    }

#if defined(PJSIP_HAS_TLS_TRANSPORT) && PJSIP_HAS_TLS_TRANSPORT!=0

    if (!pj_ansi_stricmp(tp->type_name, "tls") && info->ext_info &&
	(state == PJSIP_TP_STATE_CONNECTED || 
	 ((pjsip_tls_state_info*)info->ext_info)->
			         ssl_sock_info->verify_status != PJ_SUCCESS))
    {
	pjsip_tls_state_info *tls_info = (pjsip_tls_state_info*)info->ext_info;
	pj_ssl_sock_info *ssl_sock_info = tls_info->ssl_sock_info;
	char buf[2048];
	const char *verif_msgs[32];
	unsigned verif_msg_cnt;

	/* Dump server TLS cipher */
	PJ_LOG(4,(THIS_FILE, "TLS cipher used: 0x%06X/%s",
		  ssl_sock_info->cipher,
		  pj_ssl_cipher_name(ssl_sock_info->cipher) ));

	/* Dump server TLS certificate */
	pj_ssl_cert_info_dump(ssl_sock_info->remote_cert_info, "  ",
			      buf, sizeof(buf));
	PJ_LOG(4,(THIS_FILE, "TLS cert info of %s:\n%s", host_port, buf));

	/* Dump server TLS certificate verification result */
	verif_msg_cnt = PJ_ARRAY_SIZE(verif_msgs);
	pj_ssl_cert_get_verify_status_strings(ssl_sock_info->verify_status,
					      verif_msgs, &verif_msg_cnt);
	PJ_LOG(3,(THIS_FILE, "TLS cert verification result of %s : %s",
			     host_port,
			     (verif_msg_cnt == 1? verif_msgs[0]:"")));
	if (verif_msg_cnt > 1) {
	    unsigned i;
	    for (i = 0; i < verif_msg_cnt; ++i)
		PJ_LOG(3,(THIS_FILE, "- %s", verif_msgs[i]));
	}

	if (ssl_sock_info->verify_status &&
	    !app_config.udp_cfg.tls_setting.verify_server) 
	{
	    PJ_LOG(3,(THIS_FILE, "PJSUA is configured to ignore TLS cert "
				 "verification errors"));
	}
    }

#endif

}

/*
 * Notification on ICE error.
 */
static void on_ice_transport_error(int index, pj_ice_strans_op op,
				   pj_status_t status, void *param)
{
    PJ_UNUSED_ARG(op);
    PJ_UNUSED_ARG(param);
    PJ_PERROR(1,(THIS_FILE, status,
	         "ICE keep alive failure for transport %d", index));
}

/*
 * Notification on sound device operation.
 */
static pj_status_t on_snd_dev_operation(int operation)
{
    PJ_LOG(3,(THIS_FILE, "Turning sound device %s", (operation? "ON":"OFF")));
    return PJ_SUCCESS;
}

/*
 * Notify application on call being transfered (i.e. REFER is received). 
 * Application can decide to accept/reject transfer request by setting the code (default is 202). 
 */
static void on_call_transfer_request(pjsua_call_id call_id,
				     const pj_str_t *dst,
				     pjsip_status_code *code)
{
	PJ_LOG(3, (THIS_FILE, "Call %d is being transfered", call_id));
}

//////////////////////////////////////////////////////////////////////////
// Public API - DLL functions...
PJSIPDLL_DLL_API int dll_init()
{
	pjsua_transport_id transport_id = -1;
	pjsua_transport_config tcp_cfg;
	unsigned i;
	pj_status_t status;

	if (sipekConfigEnabled && sipek_config.pollingEventsEnabled)
	{
		// register thread 
		pj_thread_desc desc = {0};
		pj_thread_t* thread = NULL;
		pj_thread_register("GThread", desc, &thread);
	}

	/* Create pjsua */
	status = pjsua_create();
	if (status != PJ_SUCCESS)
		return status;

	/* Create pool for application */
    app_config.pool = pjsua_pool_create("pjsua", 1000, 1000);

	/* Initialize default config */
	default_config(&app_config);

	/* Parse the arguments */
//	status = parse_args(argc, argv, &app_config, &uri_arg);
//	if (status != PJ_SUCCESS)
//		return status;

	// check sipek config
	if (sipekConfigEnabled)
	{ 
		if (sipek_config.pollingEventsEnabled)
		{
			// set num of worker threads to 0		
			app_config.cfg.thread_cnt = 0; // for POLLING
			app_config.media_cfg.thread_cnt = 0; // for POLLING
		}

		app_config.log_cfg.level = sipek_config.logLevel;
		app_config.log_cfg.log_filename = pj_str(sipek_config.logFileName);

		app_config.udp_cfg.port = sipek_config.listenPort;
		app_config.no_udp =  (sipek_config.noUDP == true ? PJ_TRUE : PJ_FALSE);
		app_config.no_tcp =  (sipek_config.noTCP == true ? PJ_TRUE : PJ_FALSE);

		app_config.cfg.max_calls = sipek_config.maxCalls;

		app_config.media_cfg.snd_auto_close_time = sipek_config.soundAutoCloseTime;

		// Set VAD flag
		app_config.media_cfg.no_vad = !sipek_config.VADEnabled;
		// Set EC tail length in ms
		app_config.media_cfg.ec_tail_len = sipek_config.ECTail;

		if (sipek_config.useTLS) {
			app_config.cfg.use_srtp = PJMEDIA_SRTP_MANDATORY;
			app_config.cfg.srtp_secure_signaling = 1;
		}

#ifdef PJSIP_HAS_TLS_TRANSPORT
		app_config.use_tls = (sipek_config.useTLS == true ? PJ_TRUE : PJ_FALSE);
		if (app_config.use_tls == PJ_TRUE)
		{
			//app_config->udp_cfg.tls_setting.ca_list_file = pj_str("");
			//app_config.udp_cfg.tls_setting.cert_file = pj_str("server.crt");
			//app_config.udp_cfg.tls_setting.privkey_file = pj_str("pkey.key");
		}
#endif

		// set stun address
		if (strlen(sipek_config.stunAddress) > 0) {
			if (strstr(sipek_config.stunAddress, " ")) {
				char *token = strtok(sipek_config.stunAddress, " ");
				unsigned int cnt = 0;
				while (token != NULL) {
					app_config.cfg.stun_srv[cnt++] = pj_str(token);
					token = strtok(NULL, " ");
				}
				app_config.cfg.stun_srv_cnt = cnt;
			}
			else {
				app_config.cfg.stun_host = pj_str(sipek_config.stunAddress);
			}
		}
		// set nameserver address for DNS SRV support. Allow only 1 server.
		if (strlen(sipek_config.nameServer) > 0) 
		{
			app_config.cfg.nameserver_count = 1;
			app_config.cfg.nameserver[0] = pj_str(sipek_config.nameServer);
		}
	}

	//rtp qos
	app_config.rtp_cfg.qos_type = PJ_QOS_TYPE_VOICE;

	/* Initialize application callbacks */
	app_config.cfg.cb.on_call_state = &on_call_state;
	app_config.cfg.cb.on_call_media_state = &on_call_media_state;
	app_config.cfg.cb.on_incoming_call = &on_incoming_call;
    app_config.cfg.cb.on_call_tsx_state = &on_call_tsx_state;
    app_config.cfg.cb.on_dtmf_digit = &call_on_dtmf_callback;
	app_config.cfg.cb.on_reg_state = &on_reg_state;
	app_config.cfg.cb.on_buddy_state = &on_buddy_state;
	app_config.cfg.cb.on_pager = &on_pager;
	app_config.cfg.cb.on_typing = &on_typing;
    app_config.cfg.cb.on_call_transfer_status = &on_call_transfer_status;
	app_config.cfg.cb.on_call_transfer_request = &on_call_transfer_request;
    app_config.cfg.cb.on_call_replaced = &on_call_replaced;
    app_config.cfg.cb.on_nat_detect = &on_nat_detect;
	app_config.cfg.cb.on_transport_state = &on_transport_state;
    app_config.cfg.cb.on_ice_transport_error = &on_ice_transport_error;
    app_config.cfg.cb.on_snd_dev_operation = &on_snd_dev_operation;

	/* Initialize pjsua */
    status = pjsua_init(&app_config.cfg, &app_config.log_cfg,
			&app_config.media_cfg);
	if (status != PJ_SUCCESS)
		return status;

#ifdef STEREO_DEMO
    stereo_demo();
#endif

    /* Initialize calls data */
    for (i=0; i<PJ_ARRAY_SIZE(app_config.call_data); ++i) {
		app_config.call_data[i].timer.id = PJSUA_INVALID_ID;
		app_config.call_data[i].timer.cb = &call_timeout_callback;
    }

    /* Optionally registers WAV file */
    for (i=0; i<app_config.wav_count; ++i) {
		pjsua_player_id wav_id;

		status = pjsua_player_create(&app_config.wav_files[i], 0, 
						 &wav_id);
		if (status != PJ_SUCCESS)
			goto on_error;

		if (app_config.wav_id == PJSUA_INVALID_ID) {
			app_config.wav_id = wav_id;
			app_config.wav_port = pjsua_player_get_conf_port(app_config.wav_id);
		}
    }

    /* Optionally registers tone players */
    for (i=0; i<app_config.tone_count; i++) {
		pjmedia_port *tport;
		char name[80];
		pj_str_t label;
		pj_status_t status;

		pj_ansi_snprintf(name, sizeof(name), "tone-%d,%d",
				 app_config.tones[i].freq1, 
				 app_config.tones[i].freq2);
		label = pj_str(name);
		status = pjmedia_tonegen_create2(app_config.pool, &label,
						 8000, 1, 160, 16, 
						 PJMEDIA_TONEGEN_LOOP,  &tport);
		if (status != PJ_SUCCESS) {
			pjsua_perror(THIS_FILE, "Unable to create tone generator", status);
			goto on_error;
		}

		status = pjsua_conf_add_port(app_config.pool, tport, 
						 &app_config.tone_slots[i]);
		pj_assert(status == PJ_SUCCESS);

		status = pjmedia_tonegen_play(tport, 1, &app_config.tones[i], 0);
		pj_assert(status == PJ_SUCCESS);
    }

    /* Optionally create recorder file, if any. */
    if (app_config.rec_file.slen) {
		status = pjsua_recorder_create(&app_config.rec_file, 0, NULL, 0, 0,
				       &app_config.rec_id);
		if (status != PJ_SUCCESS)
			goto on_error;

		app_config.rec_port = pjsua_recorder_get_conf_port(app_config.rec_id);
    }

	/* Create ringback tones */
    if (app_config.no_tones == PJ_FALSE) {
		unsigned i, samples_per_frame;
		pjmedia_tone_desc tone[RING_CNT+RINGBACK_CNT];
		pj_str_t name;

		samples_per_frame = app_config.media_cfg.audio_frame_ptime * 
					app_config.media_cfg.clock_rate *
					app_config.media_cfg.channel_count / 1000;

		/* Ringback tone (call is ringing) */
		name = pj_str("ringback");
		status = pjmedia_tonegen_create2(app_config.pool, &name, 
						 app_config.media_cfg.clock_rate,
						 app_config.media_cfg.channel_count, 
						 samples_per_frame,
						 16, PJMEDIA_TONEGEN_LOOP, 
						 &app_config.ringback_port);
		if (status != PJ_SUCCESS)
			goto on_error;

		pj_bzero(&tone, sizeof(tone));
		for (i=0; i<RINGBACK_CNT; i++) {
			tone[i].freq1 = RINGBACK_FREQ1;
			tone[i].freq2 = RINGBACK_FREQ2;
			tone[i].on_msec = RINGBACK_ON;
			tone[i].off_msec = RINGBACK_OFF;
		}
		tone[RINGBACK_CNT-1].off_msec = RINGBACK_INTERVAL;

		pjmedia_tonegen_play(app_config.ringback_port, RINGBACK_CNT, tone,
					 PJMEDIA_TONEGEN_LOOP);


		status = pjsua_conf_add_port(app_config.pool, app_config.ringback_port,
						 &app_config.ringback_slot);
		if (status != PJ_SUCCESS)
			goto on_error;

		/* Ring (to alert incoming call) */
		name = pj_str("ring");
		status = pjmedia_tonegen_create2(app_config.pool, &name, 
						 app_config.media_cfg.clock_rate,
						 app_config.media_cfg.channel_count, 
						 samples_per_frame,
						 16, PJMEDIA_TONEGEN_LOOP, 
						 &app_config.ring_port);
		if (status != PJ_SUCCESS)
			goto on_error;

		for (i=0; i<RING_CNT; i++) {
			tone[i].freq1 = RING_FREQ1;
			tone[i].freq2 = RING_FREQ2;
			tone[i].on_msec = RING_ON;
			tone[i].off_msec = RING_OFF;
		}
		tone[RING_CNT-1].off_msec = RING_INTERVAL;

		pjmedia_tonegen_play(app_config.ring_port, RING_CNT, 
					 tone, PJMEDIA_TONEGEN_LOOP);

		status = pjsua_conf_add_port(app_config.pool, app_config.ring_port,
						 &app_config.ring_slot);
		if (status != PJ_SUCCESS)
			goto on_error;

    }

    pj_memcpy(&tcp_cfg, &app_config.udp_cfg, sizeof(tcp_cfg));

	/* Add UDP transport unless it's disabled. */
	if (!app_config.no_udp) {
		pjsua_acc_id aid;

		status = pjsua_transport_create(PJSIP_TRANSPORT_UDP,
					&app_config.udp_cfg, 
					&transport_id);
		if (status != PJ_SUCCESS)
			goto on_error;		

		/* Add local account */
		pjsua_acc_add_local(transport_id, PJ_TRUE, &aid);
		//pjsua_acc_set_transport(aid, transport_id);
		pjsua_acc_set_online_status(current_acc, PJ_TRUE);

		if (app_config.udp_cfg.port == 0) {
			pjsua_transport_info ti;
			pj_sockaddr_in *a;

			pjsua_transport_get_info(transport_id, &ti);
			a = (pj_sockaddr_in*)&ti.local_addr;

			tcp_cfg.port = pj_ntohs(a->sin_port);
		}
	}

	/* Add TCP transport unless it's disabled */
	if (!app_config.no_tcp) {
		status = pjsua_transport_create(PJSIP_TRANSPORT_TCP,
					&tcp_cfg, 
					&transport_id);
		if (status != PJ_SUCCESS)
			goto on_error;

		/* Add local account */
		pjsua_acc_add_local(transport_id, PJ_TRUE, NULL);
		pjsua_acc_set_online_status(current_acc, PJ_TRUE);
	}


#if defined(PJSIP_HAS_TLS_TRANSPORT) && PJSIP_HAS_TLS_TRANSPORT!=0
	/* Add TLS transport when application wants one */
	if (app_config.use_tls) {

		pjsua_acc_id acc_id;

		/* Set TLS port as TCP port+1 */
	tcp_cfg.port++;
		status = pjsua_transport_create(PJSIP_TRANSPORT_TLS,
					&tcp_cfg, 
			&transport_id);
	tcp_cfg.port--;
		if (status != PJ_SUCCESS)
			goto on_error;

		/* Add local account */
		pjsua_acc_add_local(transport_id, PJ_FALSE, &acc_id);
		pjsua_acc_set_online_status(acc_id, PJ_TRUE);
	}
#endif

	if (transport_id == -1) {
		PJ_LOG(3,(THIS_FILE, "Error: no transport is configured"));
		status = -1;
		goto on_error;
	}

	/* Add RTP transports */
	/*
	status = pjsua_media_transports_create(&app_config.rtp_cfg);
	if (status != PJ_SUCCESS)
		goto on_error;
	*/

	/* Use null sound device? */
#ifndef STEREO_DEMO
	if (app_config.null_audio) {
		status = pjsua_set_null_snd_dev();
		if (status != PJ_SUCCESS)
			return status;
	}
#endif

    if (app_config.capture_dev != PJSUA_INVALID_ID
        || app_config.playback_dev != PJSUA_INVALID_ID) {
			status = pjsua_set_snd_dev(app_config.capture_dev, app_config.playback_dev);
			if (status != PJ_SUCCESS)
				goto on_error;
    }

	//////////////////////////////////////////////////////////////////////////
	// Registering new Module for Notify handling....
	static pjsip_module MyModule; // cannot be a stack variable

	memset(&MyModule, 0, sizeof(MyModule));
	MyModule.id = -1;
	MyModule.priority = PJSIP_MOD_PRIORITY_APPLICATION+1;
	MyModule.on_rx_request = &on_rx_request;
	MyModule.name = pj_str("My-Module");

	status = pjsip_endpt_register_module(pjsip_ua_get_endpt(pjsip_ua_instance()), &MyModule);
	if (status != PJ_SUCCESS) {
		exit(1);
	}
	//////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////


	return PJ_SUCCESS;

on_error:
	app_destroy();
	return status;
}


PJSIPDLL_DLL_API int dll_main(void)
{
	pj_status_t status;

	/* Start pjsua */
	status = pjsua_start();
	if (status != PJ_SUCCESS) {
		app_destroy();
		return status;
	}

	return PJ_SUCCESS;
}

pj_status_t app_destroy(void)
{
    pj_status_t status;
    unsigned i;

#ifdef STEREO_DEMO
    if (app_config.snd) {
	pjmedia_snd_port_destroy(app_config.snd);
	app_config.snd = NULL;
    }
#endif

    /* Close ringback port */
    if (app_config.ringback_port && app_config.ringback_slot != PJSUA_INVALID_ID) {
		pjsua_conf_remove_port(app_config.ringback_slot);
		app_config.ringback_slot = PJSUA_INVALID_ID;
		pjmedia_port_destroy(app_config.ringback_port);
		app_config.ringback_port = NULL;
    }

    /* Close ring port */
    if (app_config.ring_port && app_config.ring_slot != PJSUA_INVALID_ID) {
		pjsua_conf_remove_port(app_config.ring_slot);
		app_config.ring_slot = PJSUA_INVALID_ID;
		pjmedia_port_destroy(app_config.ring_port);
		app_config.ring_port = NULL;
    }

    /* Close tone generators */
    for (i=0; i<app_config.tone_count; ++i) {
		pjsua_conf_remove_port(app_config.tone_slots[i]);
    }

    if (app_config.pool) {
		pj_pool_release(app_config.pool);
		app_config.pool = NULL;
    }

    status = pjsua_destroy();

    pj_bzero(&app_config, sizeof(app_config));

    return status;
}

//////////////////////////////////////////////////////////////////////////


PJSIPDLL_DLL_API int dll_shutdown()
{
	pj_status_t status;

	status = app_destroy();

	return status;
}

//////////////////////////////////////////////////////////////////////////
int dll_registerAccount(char* uri, char* reguri, char* domain, char* username, char* password, char* proxy, bool isdefault)
{
	pjsua_acc_config accConfig; 

	pjsua_acc_config_default(&accConfig);

	// set parameters 
	// disable contact rewrite
	accConfig.allow_contact_rewrite = 0;

	accConfig.publish_enabled = sipek_config.publishEnabled == true ? PJ_TRUE : PJ_FALSE; // enable publish
	accConfig.reg_timeout = sipek_config.expires;		

	accConfig.id = pj_str(uri);
	accConfig.reg_uri = pj_str(reguri);

	pj_str_t tmpproxy = pj_str(proxy);
	if (tmpproxy.slen > 0)
	{
		if (pjsua_verify_sip_url(proxy) != 0) {
			PJ_LOG(1,(THIS_FILE, "Error: invalid SIP URL '%s' in proxy argument", proxy));
			return PJ_EINVAL;
		}
		accConfig.proxy_cnt = 1;
		pj_strcat2(&tmpproxy, ";lr");
		accConfig.proxy[0] = tmpproxy;
	}

	// AUTHENTICATION
	accConfig.cred_count = 1;
	accConfig.cred_info[0].username = pj_str(username);
	accConfig.cred_info[0].realm = pj_str(domain);
	accConfig.cred_info[0].scheme = pj_str("Digest");
	accConfig.cred_info[0].data_type = PJSIP_CRED_DATA_PLAIN_PASSWD;
	accConfig.cred_info[0].data = pj_str(password);

	pjsua_acc_id pjAccId= -1;
	int status = pjsua_acc_add(&accConfig, isdefault == true ? PJ_TRUE : PJ_FALSE, &pjAccId);

	return pjAccId;
}

int dll_removeAccounts()
{
	pj_status_t status;
	unsigned int count = 5;
	pjsua_acc_id ids[16];

	pjsua_enum_accs( &ids[0], &count);

	for (unsigned int i=0; i<count; i++) {
		status |= pjsua_acc_del(ids[i]);
	}
	return status;
}

///////////////////////////////////////////////////////////////////////
// Call API

int dll_makeCall(int accountId, char* uri)
{
	int newcallId = -1; 

	pj_str_t sipuri = pj_str(uri);
	pjsua_call_make_call( accountId, &sipuri, 0, NULL, NULL, &newcallId);

	return newcallId;
}

int dll_releaseCall(int callId)
{
	pj_status_t status = -1;

	PJ_LOG(3, (THIS_FILE, "Releasing call %d", callId));

	status = pjsua_call_hangup(callId, 0, NULL, NULL);

	return status;
}


int dll_answerCall(int callId, int code)
{
	pjsua_call_answer(callId, code, NULL, NULL);
	return 1;
}

int dll_holdCall(int callId)
{
	pjsua_call_set_hold(callId, NULL);	
	return 1;
}

int dll_retrieveCall(int callId)
{
    pjsua_call_reinvite(callId, PJ_TRUE, NULL);
    return 1;
}

int dll_xferCall(int callid, char* uri)
{
	pjsua_msg_data msg_data;
	pjsip_generic_string_hdr refer_sub;
	pj_str_t STR_REFER_SUB = { "Refer-Sub", 9 };
	pj_str_t STR_FALSE = { "false", 5 };
	pjsua_call_info ci;

	pjsua_call_get_info(callid, &ci);

	//ui_input_url("Transfer to URL", buf, sizeof(buf), &result);

	pjsua_msg_data_init(&msg_data);
	if (app_config.no_refersub) {
		// Add Refer-Sub: false in outgoing REFER request
		pjsip_generic_string_hdr_init2(&refer_sub, &STR_REFER_SUB, &STR_FALSE);
		pj_list_push_back(&msg_data.hdr_list, &refer_sub);
	}

	pj_str_t tmp = pj_str(uri);
	pj_status_t st = pjsua_call_xfer( callid, &tmp, &msg_data);
	return st;
}

int dll_xferCallWithReplaces(int callId, int dstSession)
{
	int call = callId;
	pjsua_msg_data msg_data;
	pjsip_generic_string_hdr refer_sub;
	pj_str_t STR_REFER_SUB = { "Refer-Sub", 9 };
	pj_str_t STR_FALSE = { "false", 5 };
	//pjsua_call_id ids[PJSUA_MAX_CALLS];
	//pjsua_call_info ci;

	pjsua_msg_data_init(&msg_data);
	if (app_config.no_refersub) {
	    /* Add Refer-Sub: false in outgoing REFER request */
	    pjsip_generic_string_hdr_init2(&refer_sub, &STR_REFER_SUB, &STR_FALSE);
	    pj_list_push_back(&msg_data.hdr_list, &refer_sub);
	}

	pjsua_call_xfer_replaces(call, dstSession, 0, &msg_data);

    return 1;
}

int dll_serviceReq(int callId, int serviceCode, const char* destUri)
{
    int status = !PJ_SUCCESS; //default status is ERROR!!
    switch(serviceCode)
    {
        case SC_3Pty:
        {//as this is only local 3PTY that's all we have to do ....
            status = dll_retrieveCall(callId);
        }
  	    break;
		case SC_CFU:
		case SC_CFB:
		case SC_CFNR:
		case SC_Deflect:
            {
			//1.) build sip target Uri  
			pj_str_t contact_header_to_call = pj_str((char*)destUri);
        
			//2.) Fill pjsua_msg_data with correct Contact header ...
			pjsua_msg_data aStruct;
			pjsua_msg_data_init(&aStruct);//Initialize ...
        
			pjsip_generic_string_hdr warn;
			pj_str_t hname = pj_str("Contact");
			pj_str_t hvalue = contact_header_to_call;
			pjsip_generic_string_hdr_init2(&warn, &hname, &hvalue);
			warn.type = PJSIP_H_CONTACT;
			pj_list_push_back(&aStruct.hdr_list, &warn);
        
			//3.) Forward this call...
			//convert callId from abstract one (UI/CC) into concrete one (PJSIP) !!!
			status = pjsua_call_hangup(callId, 302, NULL, &(aStruct));
            }
			break;
        case SC_DND:
			{
			//this->handleReleaseReq(aAbstrCallId, 486);// sends 486 Busy here and releases this call instance ...
			status = pjsua_call_hangup(callId, 486, NULL, NULL);
		    }
			break;
	}//switch(serviceCode)
	return status;
}

int dll_dialDtmf(int callId, char* digits, int mode)
{
	pj_status_t status;

	// dtmf mode
	switch(mode)
	{
		case 0:
			status = dll_sendInfo(callId, digits);
			break;

		case 1:
			status = pjsua_call_dial_dtmf(callId, &pj_str(digits));
			if (status != PJ_SUCCESS) {
				pjsua_perror(THIS_FILE, "Unable to send DTMF", status);
			}
			else {
				puts("DTMF digits enqueued for transmission");
			}
			break;

		case 2:
			break;
	}

	return status;
}

////////////////////////////////////////////////////////////////////////////////////////////////
int dll_addBuddy(char* uri, bool subscribe)
{
	pj_status_t status;
	pjsua_buddy_config buddy_cfg;

	pj_str_t sipuri = pj_str(uri);

	buddy_cfg.uri = sipuri;
	buddy_cfg.subscribe = (subscribe == true) ? 1 : 0;
	// Add buddy...
	int buddyId = -1;
	status = pjsua_buddy_add(&buddy_cfg, &buddyId);
  
	// enable presence monitoring...
	if ((status == PJ_SUCCESS) && (subscribe == true)) {
		status = pjsua_buddy_subscribe_pres(buddyId, PJ_TRUE);
	}
	return buddyId;
}

int dll_removeBuddy(int buddyId)
{
	return pjsua_buddy_del(buddyId);
}

int dll_sendMessage(int accId, char* uri, char* message)
{
	pj_str_t tmp_uri = pj_str(uri);
	pj_str_t tmp = pj_str(message);
	return pjsua_im_send(accId, &tmp_uri, NULL, &tmp, NULL, NULL);
}

int dll_sendCallMessage(int callId, char* message)
{
	pj_str_t tmp = pj_str(message);
	return pjsua_call_send_im( callId, NULL, &tmp, NULL, NULL);
}

int dll_setStatus(int accId, int presence_state)
{
	pj_status_t online_status;
	pj_bool_t is_online = PJ_FALSE;
	pjrpid_element elem;

    pj_bzero(&elem, sizeof(elem));
    elem.type = PJRPID_ELEMENT_TYPE_PERSON;

    online_status = PJ_TRUE;

    switch (presence_state) {
		case AVAILABLE:
			break;
		case BUSY:
			elem.activity = PJRPID_ACTIVITY_BUSY;
			elem.note = pj_str("Busy");
			break;
		case OTP:
			elem.activity = PJRPID_ACTIVITY_BUSY;
			elem.note = pj_str("On the phone");
			break;
		case IDLE:
			elem.activity = PJRPID_ACTIVITY_UNKNOWN;
			elem.note = pj_str("Idle");
			break;
		case AWAY:
			elem.activity = PJRPID_ACTIVITY_AWAY;
			elem.note = pj_str("Away");
			break;
		case BRB:
			elem.activity = PJRPID_ACTIVITY_UNKNOWN;
			elem.note = pj_str("Be right back");
			break;
		case OFFLINE:
			online_status = PJ_FALSE;
			break;
    }

    pj_status_t status = pjsua_acc_set_online_status2(accId, online_status, &elem);

	return status;
}

int dll_sendInfo(int callid, char* content)
{
	pj_status_t status;
	pj_str_t temp;

	// allocate buffer
	temp.slen = 255;
	temp.ptr = (char*) pj_pool_alloc(app_config.pool, 255);

	pj_strcpy(&temp, &pj_str("Signal="));
	pj_strcat(&temp, &pj_str(content));

	pjsua_msg_data msg_data;
	pjsua_msg_data_init(&msg_data);  

	msg_data.content_type = pj_str("application/dtmf-relay");
	msg_data.msg_body = temp;
	pj_str_t typeInfo = pj_str("INFO");
	status = pjsua_call_send_request(callid, &typeInfo, &msg_data);

	return status;
}	

//////////////////////////////////////////////////////////////////////////////
int dll_getNumOfCodecs()
{
	pjsua_codec_info c[32];
	unsigned count = PJ_ARRAY_SIZE(c);

	pjsua_enum_codecs(c, &count);

	return count;
}

int dll_getCodec(int index, char* codec)
{
	pjsua_codec_info c[32];
	unsigned count = PJ_ARRAY_SIZE(c);

	pjsua_enum_codecs(c, &count);
	
	if (index >= (int)count) return -1;
	
	if (c[index].codec_id.slen >= 256) return -1;

	strncpy(codec , c[index].codec_id.ptr, c[index].codec_id.slen);
	codec[c[index].codec_id.slen] = 0;

	//PJ_LOG(3,(THIS_FILE,"Codec %s, prio %d", codec, c[index].priority ));

	return 1;
}	

int dll_setCodecPriority(char* name, int prio)
{
	pj_str_t id;
	pj_status_t status;

	if (prio > 0)
	{
		status = pjsua_codec_set_priority(pj_cstr(&id, name), (pj_uint8_t)prio);
	}
	else
	{
		status = pjsua_codec_set_priority(pj_cstr(&id, name), (pj_uint8_t)(PJMEDIA_CODEC_PRIO_DISABLED));
	}

  
	if (status != PJ_SUCCESS)
		PJ_LOG(3, (THIS_FILE, "Error setting codec (%s) priority %d", name, prio));
	else
		PJ_LOG(3,(THIS_FILE,"Setting codec (%s) prio: %d", name, prio));

	return 1;
}


int dll_getCurrentCodec(pjsua_call_id call_id, char* codec)
{	
	//not sure why but pjmedia/session.h is commented out from
	//pjmedia.h. so well, comment in if you want to.

	/*
	pjmedia_session_info media_info;
	pj_status_t status;

	if (pjsua_var.calls[call_id].session == NULL)
		return -1;

	status = pjmedia_session_get_info(pjsua_var.calls[call_id].session, &media_info);

	if ((status != PJ_SUCCESS) || (media_info.stream_cnt <= 0))
		return -1;

	strncpy(codec , media_info.stream_info[0].fmt.encoding_name.ptr, media_info.stream_info[0].fmt.encoding_name.slen);
	codec[media_info.stream_info[0].fmt.encoding_name.slen] = 0;
	*/
	return 0;
}


int dll_setSoundDevice(char* playbackDeviceName, char* recordingDeviceName)
{
	int capture_dev = -1;
	int playback_dev = -1;
	const pjmedia_snd_dev_info *info;
	int name_len;

    int i, count;
    
    count = pjmedia_snd_get_dev_count();
    if (count == 0) {
		return -1;
    }

    for (i=0; i<count; ++i) {
		info = pjmedia_snd_get_dev_info(i);
		name_len = strlen(info->name);
		PJ_LOG(1,(THIS_FILE, "Device %d, %s (capture=%d, playback=%d)",i, info->name, info->input_count, info->output_count));

		// check names
		// compare the first "name_len" characters since info->name does not necessarily hold full length of
		// device name.
		if ((info->input_count > 0) && (pj_strncmp2(&pj_str(recordingDeviceName), info->name, name_len) == 0))
		{
			// device found
			capture_dev	= i;
		}
		else if ((info->output_count > 0) && (pj_strncmp2(&pj_str(playbackDeviceName), info->name, name_len) == 0))
		{
			// device found
			playback_dev	= i;
		}

		pj_assert(info != NULL);
    }

	if (capture_dev == -1 || playback_dev == -1) {
		return -1;
	}

	pj_status_t status = pjsua_set_snd_dev(capture_dev, playback_dev);

	return status;	
}

///
int dll_makeConference(int callId)
{
	pjsua_call_info call_info;
	pjsua_call_id call_ids[PJSUA_MAX_CALLS];
	unsigned call_cnt=PJ_ARRAY_SIZE(call_ids);
	unsigned i;

    pjsua_call_get_info(callId, &call_info);

	/* Put call in conference with other calls */

	/* Get all calls, and establish media connection between
	    * this call and other calls.
	    */
	pjsua_enum_calls(call_ids, &call_cnt);

	for (i=0; i<call_cnt; ++i) {
		if (call_ids[i] == callId)
		    continue;
		
		if (!pjsua_call_has_media(call_ids[i]))
		    continue;

		pjsua_conf_connect(call_info.conf_slot,
				   pjsua_call_get_conf_port(call_ids[i]));
		pjsua_conf_connect(pjsua_call_get_conf_port(call_ids[i]),
				   call_info.conf_slot);

		/* Automatically record conversation, if desired */
		if (app_config.auto_rec && app_config.rec_port != PJSUA_INVALID_ID) {
		    pjsua_conf_connect(pjsua_call_get_conf_port(call_ids[i]), 
				       app_config.rec_port);
		}

	}
	return 1;
}

/////////////////////////////////////////////////////////////////////////
// SipConfig
void dll_setSipConfig(SipConfigStruct* config)
{
	sipekConfigEnabled = true;
	sipek_config = *config; 
}


//
int dll_pollForEvents(int timeout)
{
	pj_status_t status = !PJ_SUCCESS;

	status = pjsua_handle_events(timeout);
	if (0 > status)
	{
		PJ_LOG(1,(THIS_FILE, "Error handling events!"));
	}
	return status;
}