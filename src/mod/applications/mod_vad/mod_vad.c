/*
 *
 * mod_vad.c VAD code with optional libfvad
 *
 */
#include <switch.h>

#ifdef SWITCH_HAVE_FVAD
#include <fvad.h>
#endif

#define VAD_PRIVATE "_vad_"
#define VAD_XML_CONFIG "vad.conf"

SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_vad_shutdown);

SWITCH_MODULE_LOAD_FUNCTION(mod_vad_load);

SWITCH_MODULE_DEFINITION(mod_vad, mod_vad_load, mod_vad_shutdown, NULL
);

SWITCH_STANDARD_APP(vad_start_function);

struct mod_vad_s {
    // configs
    int channels;
    int sample_rate;
    int debug;
    int divisor;
    int thresh;
    int voice_samples_thresh;
    int silence_samples_thresh;

    // VAD state
    int voice_samples;
    int silence_samples;
    switch_vad_state_t vad_state;
    switch_media_bug_t *read_bug;
#ifdef SWITCH_HAVE_FVAD
    Fvad *fvad;
#endif
};

typedef struct mod_vad_s mod_vad_t;

///**
// * module globals - global configuration and variables
// */
//static mod_vad_globals {
//    int mode;
//    char *debug;
//};
//typedef struct mod_vad_globals mod_vad_globals_t;
//
///** Module global variables */
//static mod_vad_globals_t globals;
//
///** init mod_vad config*/
//static switch_status_t mod_vad_do_config(void)
//{
//    switch_xml_t cfg, xml, settings, param;
//    switch_status_t status = SWITCH_STATUS_SUCCESS;
//
//    if(!(xml = switch_xml_open_cfg(VAD_XML_CONFIG, &cfg, NULL))) {
//        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Failed to open XML configuration '%s'\n", VAD_XML_CONFIG);
//        status = SWITCH_STATUS_FALSE;
//        goto done;
//    }
//
//    if((settings = switch_xml_child(cfg, "settings"))) {
//        for(param = switch_xml_child(settings, "param"); param; param = param->next) {
//            char *var = (char *)switch_xml_attr_soft(param, "name");
//            char *val = (char *)switch_xml_attr_soft(param, "value");
//            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Found parameter %s=%s\n", var, val);
//            if(!strcasecmp(var, "mode")) {
//                globals.mode = atoi(val);
//            } else if(!strcasecmp(var, "debug")) {
//                globals.debug = val;
//            }
//        }
//    }
//
//    done:
//
//    if (xml) {
//        switch_xml_free(xml);
//    }
//
//    return status;
//}

SWITCH_DECLARE(void) mod_vad_reset(mod_vad_t *vad) {
#ifdef SWITCH_HAVE_FVAD

    if (vad->fvad) {
        fvad_reset(vad->fvad);
    }
#endif
    vad->vad_state = SWITCH_VAD_STATE_NONE;
    vad->voice_samples = 0;
    vad->silence_samples = 0;

    if (vad->debug) switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "reset vad state\n");
}

SWITCH_DECLARE(const char *)mod_vad_state2str(switch_vad_state_t state) {
    switch (state) {
        case SWITCH_VAD_STATE_NONE:
            return "none";
        case SWITCH_VAD_STATE_START_TALKING:
            return "start_talking";
        case SWITCH_VAD_STATE_TALKING:
            return "talking";
        case SWITCH_VAD_STATE_STOP_TALKING:
            return "stop_talking";
        default:
            return "error";
    }
}

SWITCH_DECLARE(mod_vad_t *)mod_vad_init(int sample_rate, int channels) {
    mod_vad_t *vad = malloc(sizeof(mod_vad_t));

    if (!vad) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "fail malloc mod_vad\n");
        return NULL;
    }

    memset(vad, 0, sizeof(*vad));
    vad->sample_rate = sample_rate ? sample_rate : 8000;
    vad->channels = channels;
    vad->silence_samples_thresh = 500 * (vad->sample_rate / 1000);
    vad->voice_samples_thresh = 200 * (vad->sample_rate / 1000);
    vad->thresh = 100;
    vad->divisor = vad->sample_rate / 8000;
    if (vad->divisor <= 0) {
        vad->divisor = 1;
    }
    mod_vad_reset(vad);

    return vad;
}

SWITCH_DECLARE(int) mod_vad_set_mode(mod_vad_t *vad, int mode) {
#ifdef SWITCH_HAVE_FVAD
    int ret = 0;

    if (mode < 0) {
        if (vad->fvad) fvad_free(vad->fvad);

        vad->fvad = NULL;
        return ret;
    } else if (mode > 3) {
        mode = 3;
    }

    if (!vad->fvad) {
        vad->fvad = fvad_new();

        if (!vad->fvad) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "libfvad init error\n");
        }
    }

    if (vad->fvad) {
        ret = fvad_set_mode(vad->fvad, mode);
        fvad_set_sample_rate(vad->fvad, vad->sample_rate);
    }

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "libfvad started, mode = %d\n", mode);
    return ret;
#else
    if (vad->debug) switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "set vad mode = %d\n", mode);

    return 0;
#endif
}

SWITCH_DECLARE(void) mod_vad_set_param(mod_vad_t *vad, const char *key, int val) {
    if (!key) return;

    if (!strcmp(key, "hangover_len")) {
        /* convert old-style hits to samples assuming 20ms ptime */
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING,
                          "hangover_len is deprecated, setting silence_ms to %d\n", 20 * val);
        mod_vad_set_param(vad, "silence_ms", val * 20);
    } else if (!strcmp(key, "silence_ms")) {
        if (val > 0) {
            vad->silence_samples_thresh = val * (vad->sample_rate / 1000);
        } else {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "Ignoring invalid silence_ms of %d\n", val);
        }
    } else if (!strcmp(key, "thresh")) {
        vad->thresh = val;
    } else if (!strcmp(key, "debug")) {
        vad->debug = val;
    } else if (!strcmp(key, "voice_ms")) {
        if (val > 0) {
            vad->voice_samples_thresh = val * (vad->sample_rate / 1000);
        } else {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "Ignoring invalid voice_ms of %d\n", val);
        }
    } else if (!strcmp(key, "listen_hits")) {
        /* convert old-style hits to samples assuming 20ms ptime */
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "listen_hits is deprecated, setting voice_ms to %d\n",
                          20 * val);
        mod_vad_set_param(vad, "voice_ms", 20 * val);
    }

    if (vad->debug) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "set %s to %d\n", key, val);
    }
}

SWITCH_DECLARE(switch_vad_state_t) mod_vad_process(mod_vad_t *vad, int16_t *data, unsigned int samples) {
    int score = 0;

// Each frame has 2 possible outcomes- voice or not voice.
// The VAD has 2 real states- talking / not talking with
// begin talking and stop talking as events to mark transitions


// determine if this is a voice or non-voice frame
#ifdef SWITCH_HAVE_FVAD
    if (vad->fvad) {
        // fvad returns -1, 0, or 1
        // -1: error
        //  0: non-voice frame
        //  1: voice frame
        int ret = fvad_process(vad->fvad, data, samples);

        // if voice frame set score > threshold
        score = ret > 0 ? vad->thresh + 100 : 0;
    } else {
#endif
    int energy = 0, j = 0, count = 0;
    for (energy = 0, j = 0, count = 0; count < samples; count++) {
        energy += abs(data[j]);
        j += vad->channels;
    }

    score = (uint32_t) (energy / (samples / vad->divisor));
#ifdef SWITCH_HAVE_FVAD
    }
#endif

    if (vad->debug > 9) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "score: %d\n", score);
    }

// clear the STOP/START TALKING events
    if (vad->vad_state == SWITCH_VAD_STATE_STOP_TALKING) {
        vad->vad_state = SWITCH_VAD_STATE_NONE;
    } else if (vad->vad_state == SWITCH_VAD_STATE_START_TALKING) {
        vad->vad_state = SWITCH_VAD_STATE_TALKING;
    }

// adjust voice/silence run length counters
    if (score > vad->thresh) {
        vad->silence_samples = 0;
        vad->voice_samples += samples;
    } else {
        vad->silence_samples += samples;
        vad->voice_samples = 0;
    }

// check for state transitions
    if (vad->vad_state == SWITCH_VAD_STATE_TALKING && vad->silence_samples > vad->silence_samples_thresh) {
        vad->vad_state = SWITCH_VAD_STATE_STOP_TALKING;
        if (vad->debug) switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "mod vad state STOP_TALKING\n");
    } else if (vad->vad_state == SWITCH_VAD_STATE_NONE && vad->voice_samples > vad->voice_samples_thresh) {
        vad->vad_state = SWITCH_VAD_STATE_START_TALKING;
        if (vad->debug)
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "mod vad state START_TALKING\n");
    }

    if (vad->debug > 9)
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "vad state %s\n", mod_vad_state2str(vad->vad_state));

    return vad->vad_state;
}

SWITCH_DECLARE(switch_vad_state_t) mod_vad_get_state(mod_vad_t *vad) {
    return vad->vad_state;
}

SWITCH_DECLARE(void) mod_vad_destroy(mod_vad_t **vad) {
    if (*vad) {

#ifdef SWITCH_HAVE_FVAD
        if ((*vad)->fvad) fvad_free ((*vad)->fvad);
#endif

        free(*vad);
        *vad = NULL;
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "free vad\n");
    } else {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "free vad fail\n");
    }
}

static switch_bool_t fire_vad_event(switch_core_session_t *session, switch_vad_state_t vad_state) {
    switch_event_t *event = NULL;
    switch_channel_t *channel = switch_core_session_get_channel(session);

    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "Fire VAD event %d\n", vad_state);

    switch (vad_state) {
        case SWITCH_VAD_STATE_START_TALKING:
            if (switch_event_create(&event, SWITCH_EVENT_TALK) == SWITCH_STATUS_SUCCESS) {
                switch_channel_event_set_data(channel, event);
                switch_event_fire(&event);
            }
            break;
        case SWITCH_VAD_STATE_STOP_TALKING:
            if (switch_event_create(&event, SWITCH_EVENT_NOTALK) == SWITCH_STATUS_SUCCESS) {
                switch_channel_event_set_data(channel, event);
                switch_event_fire(&event);
            }
            break;
        default:
            break;
    }

    return SWITCH_TRUE;
}

static switch_bool_t vad_audio_callback(switch_media_bug_t *bug, void *user_data, switch_abc_type_t type) {
    mod_vad_t *vad = (mod_vad_t *) user_data;
    switch_core_session_t *session = switch_core_media_bug_get_session(bug);
    switch_vad_state_t vad_state;
    switch_frame_t *frame = {0};
    switch_channel_t *channel = switch_core_session_get_channel(session);

    switch (type) {
        case SWITCH_ABC_TYPE_INIT: {
            if (!switch_channel_media_ready(channel)) {
                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "INIT Channel codec isn't ready\n");
                return SWITCH_FALSE;
            }
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO,
                              "Starting VAD detection for audio stream\n");
            break;
        }
        case SWITCH_ABC_TYPE_CLOSE: {
            mod_vad_destroy(&vad);
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO,
                              "Stopping VAD detection for audio stream\n");
            break;
        }
        case SWITCH_ABC_TYPE_READ:
        case SWITCH_ABC_TYPE_READ_REPLACE: {
            if (!switch_channel_media_ready(channel)) {
                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "READ Channel codec isn't ready\n");
                return SWITCH_FALSE;
            }

            frame = switch_core_media_bug_get_read_replace_frame(bug);
            vad_state = mod_vad_process(vad, frame->data, frame->datalen / 2);
            if (vad_state == SWITCH_VAD_STATE_START_TALKING) {
                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "START TALKING\n");
                fire_vad_event(session, vad_state);
            } else if (vad_state == SWITCH_VAD_STATE_STOP_TALKING) {
                mod_vad_reset(vad);
                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "STOP TALKING\n");
                fire_vad_event(session, vad_state);
            } else if (vad_state == SWITCH_VAD_STATE_TALKING) {
                // talking时不做任何处理
            }
            break;
        }
        default: {
            break;
        }
    }

    return SWITCH_TRUE;
}

SWITCH_MODULE_LOAD_FUNCTION(mod_vad_load) {
    switch_application_interface_t *app_interface;
    *module_interface = switch_loadable_module_create_module_interface(pool, modname);

//    if(load_config()) {
//        return SWITCH_STATUS_UNLOAD;
//    }
#ifdef SWITCH_HAVE_FVAD
   switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "load mod_vad base libfvad\n");
#endif

    SWITCH_ADD_APP(app_interface, "vad", "Voice activity detection", "Freeswitch's VAD", vad_start_function,
                   "[start|stop]", SAF_NONE);
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, " vad_load successful...\n");

    return SWITCH_STATUS_SUCCESS;
}

SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_vad_shutdown) {
    return SWITCH_STATUS_SUCCESS;
}

SWITCH_STANDARD_APP(vad_start_function) {
    switch_status_t status;
    switch_channel_t *channel = switch_core_session_get_channel(session);
    mod_vad_t *vad = NULL;
    switch_codec_implementation_t imp = {0};
    int flags = 0;

    if (!zstr(data)) {
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "VAD input parameter %s\n", data);
    }

    if ((vad = (mod_vad_t *) switch_channel_get_private(channel, VAD_PRIVATE))) {
        if (!zstr(data) && !strcasecmp(data, "stop")) {
            switch_channel_set_private(channel, VAD_PRIVATE, NULL);
            if (vad->read_bug) {
                switch_core_media_bug_remove(session, &vad->read_bug);
                // vad->read_bug = NULL;
            }
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "Stopped VAD detection\n");
        } else {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_WARNING,
                              "Cannot run vad detection 2 times on the same session!\n");
        }
        return;
    }

    if (!zstr(data) && !strcasecmp(data, "stop")) {
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_WARNING,
                          "the session cannot start vad !\n");
        return;
    }

    switch_core_session_get_read_impl(session, &imp);
    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "Read imp %u %u.\n", imp.samples_per_second,
                      imp.number_of_channels);

    // init vad
    vad = mod_vad_init(imp.samples_per_second, imp.number_of_channels);

    switch_assert(vad);

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "mod_vad malloc vad\n");

    // 设置vad模式
    mod_vad_set_mode(vad, 3);

    // 设置是否开启debug
    mod_vad_set_param(vad, "debug", 1);

    flags = SMBF_READ_REPLACE | SMBF_ANSWER_REQ;
    status = switch_core_media_bug_add(session, "vad_read", NULL, vad_audio_callback, vad, 0, flags,
                                           &vad->read_bug);

    if (status != SWITCH_STATUS_SUCCESS) {
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR,
                              "Failed to attach vad to media stream!\n");
        mod_vad_destroy(&vad);
        return;
    }

    switch_channel_set_private(channel, VAD_PRIVATE, vad);
}
