/**
 * @file aulevels.c  Audio gain
 *
 * Copyright (C) 2025 Victor Kozub github.com/xen1024
 */

#include <stdlib.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "aulevels.h"

#include "mapi.c"

#define DEBUG_AULEVELS 0

/**
 * Audio levels module
 *
 * This module can be used to increase volume of audio channel.
 *
 * Sample config:
 *
 \verbatim
  aulevels            d, 0.5, 0.0
  aulevels            e, 0.0, 1.0
 \endverbatim
 */

struct aulevels_enc {
	struct aufilt_enc_st af;  /* base class */
	struct aufilt_prm prm;
};

struct aulevels_dec {
	struct aufilt_dec_st af;  /* base class */
	struct aufilt_prm prm;
};

// Audio buffer utilities

#define CH_MAX 2 // max 2 channels

void aubuf_gain_s16le(int16_t *sampv, size_t sampc, double gain[CH_MAX], int ch);

// CHANNELS [

#define CHANNELS_MAX 128

//#define USE_CHANNELS_ARRAY // use array or mapi
#define USE_CHANNELS_MAPI // use mapi

typedef struct {
	// For select from encode/decode frame
	void *p;
	// For select from command
	const struct audio *au;
	// Index in channels
	int index;
	// Gain
	double gain_enc[CH_MAX];
	double gain_dec[CH_MAX];
} CHANNEL_GAIN;

#ifdef USE_CHANNELS_ARRAY
CHANNEL_GAIN channels[CHANNELS_MAX] = {0};
#endif

// CHANNELS ]
// CHANNEL FUNCTIONS [

// Channel def

CHANNEL_GAIN *find_channel(void *p);
CHANNEL_GAIN *alloc_channel(void *p);
void dealloc_channel(void *p);
int channels_apply_gain(struct audio *au, double gain_enc[CH_MAX], double gain_dec[CH_MAX]);

// CHANNELS ARRAY [

#ifdef USE_CHANNELS_ARRAY

// Channel impl

CHANNEL_GAIN *find_channel(void *p) {
	for (int i = 0; i < CHANNELS_MAX; i++) {
		CHANNEL_GAIN *channel = &channels[i];
		if (channel->p == p) {
			channel->index = i; // hack
			return channel;
		}
	}
	return NULL;
}

CHANNEL_GAIN *alloc_channel(void *p) {
	CHANNEL_GAIN *channel = find_channel(NULL);
	if (!channel) {
		info("Can't allocate channel for %p\n", p);
		return NULL;
	}
	// Set gain to all channels 1.0
	for (int i = 0; i < CH_MAX; i++) {
		channel->gain_enc[i] = 1.0;
		channel->gain_dec[i] = 1.0;
	}

	channel->p = p;
	return channel;
}

void dealloc_channel(void *p) {
	CHANNEL_GAIN *channel = find_channel(p);
	if (!channel) {
		info("Can't dealloc channel for %p\n", p);
		return;
	}
	channel->p = NULL;
}

// Apply gain for channels with au selector for multiple channels
int channels_apply_gain(struct audio *au, double gain_enc[CH_MAX], double gain_dec[CH_MAX]) {
	int count = 0;
	for (int i = 0; i < CHANNELS_MAX; i++) {
		CHANNEL_GAIN *channel = &channels[i];
		if (channel->au == au) {
			for (int ich = 0; ich < CH_MAX; ich++) {
				if (gain_enc[ich] >= 0) {
					channel->gain_enc[ich] = gain_enc[ich];
				}
				if (gain_dec[ich] >= 0) {
					channel->gain_dec[ich] = gain_dec[ich];
				}
			}
			count++;
		}
	}
	return count;
}

#endif // USE_CHANNELS_ARRAY

// CHANNELS ARRAY ]
// CHANNELS MAPI [

#ifdef USE_CHANNELS_MAPI

struct hash *mapi_lazy_init(void);


static struct hash *channels_map = NULL;

struct hash *mapi_lazy_init(void) {
	if (!channels_map)
		return channels_map;

	mapi_alloc(&channels_map);
	return channels_map;
}

CHANNEL_GAIN *find_channel(void *p) {
	uint32_t key = p;

	struct le *found = mapi_get(channels_map, key);
    if (found) {
        printf("Found: %i -> %p\n", key, (char *)found->data);
    }

	CHANNEL_GAIN *data = found ? found->data : NULL;
	return data;
}

CHANNEL_GAIN *alloc_channel(void *p) {
	uint32_t key = p;
	CHANNEL_GAIN *channel = calloc(sizeof(*channel), 1);

	if (channels_map) {
		struct le element;
		mapi_insert(channels_map, key, channel, &element);
	}
	return channel;
}

void dealloc_channel(void *p) {
}

// Apply gain for channels with au selector for multiple channels
int channels_apply_gain(struct audio *au, double gain_enc[CH_MAX], double gain_dec[CH_MAX]) {
	// for each channel
	/*
		if (channel->au == au) {
			for (int ich = 0; ich < CH_MAX; ich++) {
				if (gain_enc[ich] >= 0) {
					channel->gain_enc[ich] = gain_enc[ich];
				}
				if (gain_dec[ich] >= 0) {
					channel->gain_dec[ich] = gain_dec[ich];
				}
			}
		}
	*/
}

#endif // USE_CHANNELS_MAPI

// CHANNELS MAPI ]
// CHANNEL FUNCTIONS ]
// DESTRUCTOR [

static void enc_destructor(void *arg)
{
	struct aulevels_enc *st = arg;

	dealloc_channel(st);

	list_unlink(&st->af.le);
}

// DESTRUCTOR ]
// AUBUF GAIN [

void aubuf_gain_s16le(int16_t *sampv, size_t sampc, double gain[CH_MAX], int ch) {
    for (size_t i = 0; i < sampc; i++) {
        // Apply Gain 
		int ich = i % ch;
        double sample = sampv[i] * gain[ich];
        
        // Clamp to 16-bit range
        sample = (sample < INT16_MAX) ? sample : INT16_MAX;
        sample = (sample > INT16_MIN) ? sample : INT16_MIN;
        sampv[i] = sample;
    }
}

// AUBUF GAIN ]
// ENCODE_UPDATE [

static int encode_update(struct aufilt_enc_st **stp, void **ctx,
			 const struct aufilt *af, struct aufilt_prm *prm,
			 const struct audio *au)
{
	struct aulevels_enc *st;
	(void)ctx;
	(void)af;
	(void)au;

	if (!stp || !prm)
		return EINVAL;

	if (prm->fmt != AUFMT_S16LE) {
		warning("aulevels: format not supported (%s)\n",
			aufmt_name(prm->fmt));
		return ENOTSUP;
	}

	st = mem_zalloc(sizeof(*st), enc_destructor);
	if (!st)
		return EINVAL;

	st->prm = *prm;

	*stp = (struct aufilt_enc_st *)st;

	CHANNEL_GAIN *channel = alloc_channel(st);
	if (!channel) {
		warning("aulevels: can't allocate channel for (%p)\n", st);
		return EINVAL;
	}

	channel->au = au;

	info("ENCODE ALLOC %p channel=%i au=%p \n", st, channel->index, au);

	return 0;
}

// ENCODE_UPDATE ]
// DECODE_UPDATE [

static int decode_update(struct aufilt_dec_st **stp, void **ctx,
			 const struct aufilt *af, struct aufilt_prm *prm,
			 const struct audio *au)
{
	struct aulevels_enc *st;
	(void)ctx;
	(void)af;
	(void)au;

	if (!stp || !prm)
		return EINVAL;

	if (prm->fmt != AUFMT_S16LE) {
		warning("aulevels: format not supported (%s)\n",
			aufmt_name(prm->fmt));
		return ENOTSUP;
	}

	st = mem_zalloc(sizeof(*st), enc_destructor);
	if (!st)
		return EINVAL;

	st->prm = *prm;

	*stp = (struct aufilt_dec_st *)st;

	CHANNEL_GAIN *channel = alloc_channel(st);

	if (!channel) {
		warning("aulevels: can't allocate channel for (%p)\n", st);
		return EINVAL;
	}

	channel->au = au;

	info("DECODE ALLOC %p channel=%i au=%p \n", st, channel->index, au);

	return  0;
}

// DECODE_UPDATE ]
// ENCODE_FRAME [

static int encode_frame(struct aufilt_enc_st *st, struct auframe *af)
{
	// ENCODE GAIN [

	CHANNEL_GAIN *channel = find_channel(st);
	if (!channel) {
		info("ENC: Can't find channel for %p\n", st);
		return -1;
	}

	// ENCODE GAIN ]

	if (!st || !af || !af->sampv || !af->sampc)
		return EINVAL;

	if (af->fmt == AUFMT_S16LE) {
		info("ENC: ch %i sampc %i\n", af->ch, (int)af->sampc);
        aubuf_gain_s16le(af->sampv, af->sampc, channel->gain_enc, af->ch);
    }

	return 0;
}

// ENCODE_FRAME ]
// DECODE_FRAME [

static int decode_frame(struct aufilt_dec_st *aufilt_dec_st, struct auframe *af)
{
	struct aulevels_dec *st = (struct aulevels_dec *)aufilt_dec_st;

	if (!st || !af || !af->sampv || !af->sampc)
		return EINVAL;

	// Only S16LE
	if (st->prm.fmt != AUFMT_S16LE) {
		warning("aulevels: format not supported (%s)\n",
			aufmt_name(st->prm.fmt));
		return -1;
	}

	// DECODE GAIN [

	CHANNEL_GAIN *channel = find_channel(st);
	if (!channel) {
		info("DEC: Can't find channel for %p\n", st);
		return -1;
	}

	if (af->fmt == AUFMT_S16LE) {
        aubuf_gain_s16le(af->sampv, af->sampc, channel->gain_dec, af->ch);
    }
	
	#if DEBUG_AULEVELS
	static int qqq = 0;
	if (qqq++ % 20==0) // Won't qqq overflow at some point?
		info("DECODE_FRAME %p channel=%i gain_dec=%lf\n", st, channel->index, gain_dec);
	#endif

	// DECODE GAIN ]

	return 0;
}

// DECODE_FRAME ]

static struct aufilt aulevels = {
	.name    = "aulevels",
	.encupdh = encode_update,
	.ench    = encode_frame,
	.decupdh = decode_update,
	.dech	  = decode_frame
};

// COMMAND /aulevelsch [

static int cmd_aulevels_channel(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = (struct cmd_arg *)arg;
	(void)pf;
	double new_gain[CH_MAX] = {0.0};

//	int channel;
	char cid[1024];
	char inout;
    int nparams = carg->prm ? sscanf(carg->prm, "%1023s %c %lf %lf", cid, &inout, &new_gain[0], &new_gain[1]) : 0;
	if (nparams < 3) {
        re_hprintf(pf, "Usage: aulevels <call_id> <e|d> <left> [right]\n");
        return EINVAL;
    }

	if (new_gain[0] < 0.0) {
		warning("aulevels: invalid gain value left %i\n", new_gain[0]);
		return EINVAL;
	}

	if (nparams > 3) {
		// STEREO
		if (new_gain[1] < 0.0) {
			warning("aulevels: invalid gain value right %i\n", new_gain[1]);
			return EINVAL;
		}
	}
	else {
		// MONO
		new_gain[1] = new_gain[0];
	}

	// APPLY GAIN [

	double gain_enc[CH_MAX] = {-1, -1}; // Only stereo 
	double gain_dec[CH_MAX] = {-1, -1}; // Only stereo 

	if (inout == 'e') {
		// ENCODE
		gain_enc[0] = new_gain[0];
		gain_enc[1] = new_gain[1];
	}
	else if (inout == 'd') {
		// DECODE
		gain_dec[0] = new_gain[0];
		gain_dec[1] = new_gain[1];
	}
	else {
		warning("aulevels: invalid encode or decode type. use 'e' or 'd'\n", inout);
		return EINVAL;
	}

    // For each User-Agent 
	for (struct le *le = list_head(uag_list()); le; le = le->next) {
		struct ua *ua = le->data;

		struct list *calls = ua_calls(ua);

        if (!calls) {
			warning("aulevels: calls is null for ua %p\n", ua);
			continue;
		}
		
		struct le *ale;

        // For each call
		for (ale = list_head(calls); ale; ale = ale->next) {
			struct call *call = ale->data;

			if (!call) {
				warning("aulevels: call is null for ua %p\n", ua);
				continue;
			}

			int established = call_state(call) == CALL_STATE_ESTABLISHED;

			if (!established) {
				continue;
			}

            // Get audio for the call
			struct audio *au = call_audio(call);

			if (!au) {
				warning("aulevels: au is null for call %p\n", call);
				continue;
			}

			const char *this_id = call_id(call); // 
//			const char *this_id = call_peeruri(call); // remote

            // Compare call_id and selector cid
			if (str_cmp(this_id, cid)) {
				warning("aulevels: skip call->call_id %s != cid %s\n", this_id, cid);
				continue;
			}

			info("call au=%p established=%i\n", au, established);

			channels_apply_gain(au, gain_enc, gain_dec);
		}
	}

	// APPLY GAIN ]

	info("aulevels: new %s gain is L=%.2f R=%.2f channel.cid=%s\n", 
		(inout == 'e') ? "encode" : "decode",
		new_gain[0], new_gain[1], cid);

	return 0;
}

// COMMAND /aulevelsch ]
// COMMANDS [

static const struct cmd cmdv[] = {
	{"aulevels", 0, CMD_PRM, "Set volume for the channel. aulevels <channel> <inout> <gain>", cmd_aulevels_channel},
};

// COMMANDS ]

static int module_init(void)
{
	aufilt_register(baresip_aufiltl(), &aulevels);

    char s[16384];
    conf_get_str(conf_cur(), "aulevels", s, sizeof(s));

	info("aulevels: config %s\n", s);

	return cmd_register(baresip_commands(), cmdv, RE_ARRAY_SIZE(cmdv));
}


static int module_close(void)
{
	aufilt_unregister(&aulevels);
	
	cmd_unregister(baresip_commands(), cmdv);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(aulevels) = {
	"aulevels",
	"filter",
	module_init,
	module_close
};
