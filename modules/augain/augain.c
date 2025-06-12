/**
 * @file augain.c  Audio gain
 *
 * Copyright (C) 2024 Juha Heinanen
 */

#include <stdlib.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
//#include <string.h>

#define DEBUG_GAIN 0

/**
 * Audio gain module
 *
 * This module can be used to increase volume of audio source,
 * for example, microphone.
 *
 * Sample config:
 *
 \verbatim
  augain            1.5
 \endverbatim
 */

struct augain_enc {
	struct aufilt_enc_st af;  /* base class */
	struct aufilt_prm prm;
};

struct augain_dec {
	struct aufilt_dec_st af;  /* base class */
	struct aufilt_prm prm;
};

static double gain = 1.0;

// CHANNELS [

#define CHANNELS_MAX 128

typedef struct {
	// For select from encode/decode frame
	void *p;
	// For select from command
	const struct audio *au;
	// Index in channels
	int index;
	// Gain
	double gain_enc;
	double gain_dec;
} CHANNEL_GAIN;

CHANNEL_GAIN channels[CHANNELS_MAX] = {0};

// CHANNELS ]
// CHANNEL FUNCTIONS [

// Channel def

CHANNEL_GAIN *find_channel(void *p);
CHANNEL_GAIN *alloc_channel(void *p);
void dealloc_channel(void *p);
int channels_apply_gain(struct audio *au, double gain_enc, double gain_dec);

// Channel impl

CHANNEL_GAIN *find_channel(void *p) {
	for (int i = 0; i != CHANNELS_MAX; i++) {
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
	channel->gain_enc = 1.0;
	channel->gain_dec = 1.0;
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

int channels_apply_gain(struct audio *au, double gain_enc, double gain_dec) {
	int count = 0;
	for (int i = 0; i != CHANNELS_MAX; i++) {
		CHANNEL_GAIN *channel = &channels[i];
		if (channel->au == au) {
			if (gain_enc >= 0) {
				channel->gain_enc = gain_enc;
			}
			if (gain_dec >= 0) {
				channel->gain_dec = gain_dec;
			}
			count++;
		}
	}
	return count;
}

// CHANNEL FUNCTIONS ]
// DESTRUCTOR [

static void enc_destructor(void *arg)
{
	struct augain_enc *st = arg;

	dealloc_channel(st);

	list_unlink(&st->af.le);
}

// DESTRUCTOR ]
// UA - MENU [

struct filter_arg {
	enum call_state state;
	const struct call *exclude;
	const struct call *match;
	struct call *call;
};
/*
static void find_first_call(struct call *call, void *arg)
{
	struct filter_arg *fa = arg;

	if (!fa->call)
		fa->call = call;
}

static bool filter_call(const struct call *call, void *arg)
{
	struct filter_arg *fa = arg;

	if (fa->state != CALL_STATE_UNKNOWN && call_state(call) != fa->state)
		return false;

	if (call == fa->exclude)
		return false;

	if (fa->match && call != fa->match)
		return false;

	return true;
}

static bool active_call_test(const struct call* call, void *arg)
{
	struct filter_arg *fa = arg;

	if (call == fa->exclude)
		return false;

	return call_state(call) == CALL_STATE_ESTABLISHED &&
			!call_is_onhold(call);
}
*/
// UA - MENU ]
// ENCODE_UPDATE [

static int encode_update(struct aufilt_enc_st **stp, void **ctx,
			 const struct aufilt *af, struct aufilt_prm *prm,
			 const struct audio *au)
{
	struct augain_enc *st;
	(void)ctx;
	(void)af;
	(void)au;

	if (!stp || !prm)
		return EINVAL;

	if (prm->fmt != AUFMT_S16LE) {
		warning("augain: format not supported (%s)\n",
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
		warning("augain: can't allocate channel for (%p)\n", st);
		return EINVAL;
	}

	channel->au = au;

	info("ENCODE ALLOC %p channel=%i au=%p \n", st, channel->index, au);

	return  0;
}

// ENCODE_UPDATE ]
// DECODE_UPDATE [

static int decode_update(struct aufilt_dec_st **stp, void **ctx,
			 const struct aufilt *af, struct aufilt_prm *prm,
			 const struct audio *au)
{
	struct augain_enc *st;
	(void)ctx;
	(void)af;
	(void)au;

	if (!stp || !prm)
		return EINVAL;

	if (prm->fmt != AUFMT_S16LE) {
		warning("augain: format not supported (%s)\n",
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
		warning("augain: can't allocate channel for (%p)\n", st);
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
	unsigned int abs_sample, highest_abs_sample = 0;
	double highest_possible_gain;
	int16_t sample, gained_sample;

	// ENCODE GAIN [

	CHANNEL_GAIN *channel = find_channel(st);
	if (!channel) {
		info("ENC: Can't find channel for %p\n", st);
		return -1;
	}

	double encode_gain = channel->gain_enc;

	// ENCODE GAIN ]

	if (!st || !af || !af->sampv || !af->sampc)
		return EINVAL;

	for (size_t i=0; i<af->sampc; i++) { // FIXME! Can't determine max gain from one batch of samples, it would be different every time. Just clamp it.
		abs_sample = abs(((int16_t *)(af->sampv))[i]);
		if (abs_sample > highest_abs_sample)
			highest_abs_sample = abs_sample;
	}

	highest_possible_gain = 32767.0 / highest_abs_sample;
	if (encode_gain > highest_possible_gain)
		encode_gain = highest_possible_gain;

	for (size_t i=0; i<af->sampc; i++) {
		sample = ((int16_t *)(af->sampv))[i];
		gained_sample = (int16_t)(sample * encode_gain);
		((int16_t *)(af->sampv))[i] = gained_sample;
	}

	return 0;
}

// ENCODE_FRAME ]
// DECODE_FRAME [

static int decode_frame(struct aufilt_dec_st *aufilt_dec_st, struct auframe *af)
{
	struct augain_dec *dec = (struct augain_dec *)aufilt_dec_st;

	struct augain_dec *st = (struct augain_dec *)aufilt_dec_st;

//	info("DEC:  %p\n", st);

	if (!st || !af || !af->sampv || !af->sampc)
		return EINVAL;

	// Only S16LE
	if (dec->prm.fmt != AUFMT_S16LE) {
		warning("augain: format not supported (%s)\n",
			aufmt_name(dec->prm.fmt));
		return -1;
	}

	// DECODE GAIN [

	CHANNEL_GAIN *channel = find_channel(st);
	if (!channel) {
		info("DEC: Can't find channel for %p\n", st);
		return -1;
	}

	double gain_dec = channel->gain_dec;

	// DECODE GAIN ]

	#if DEBUG_GAIN
	static int qqq = 0;
	if (qqq++ % 20==0) // Won't qqq overflow at some point?
		info("DECODE_FRAME %p channel=%i gain_dec=%lf\n", st, channel->index, gain_dec);
	#endif

	if (af->fmt == AUFMT_S16LE) {
        int16_t *sampv = (int16_t *)af->sampv;
        for (size_t i = 0; i < af->sampc; i++) {
			
			// Apply Gain 
            int32_t sample = sampv[i] * gain_dec;
            
			// Clamp to 16-bit range
            sample = (sample < INT16_MAX) ? sample : INT16_MAX;
            sample = (sample > INT16_MIN) ? sample : INT16_MIN;
            sampv[i] = sample;
        }
    }
	
	return 0;
}

// DECODE_FRAME ]

static struct aufilt augain = {
	.name    = "augain",
	.encupdh = encode_update,
	.ench    = encode_frame,
//	.decupdh = NULL,
//	.dech	  = NULL
	.decupdh = decode_update,
	.dech	  = decode_frame
};

// COMMAND /augain [

static int cmd_augain(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = (struct cmd_arg *)arg;
	(void)pf;
	double new_gain = 0.0;

	if (str_isset(carg->prm))
		new_gain = strtod(carg->prm, NULL);

	if (new_gain < 0.0) {
		warning("augain: invalid gain value %s\n", new_gain);
		return EINVAL;
	}

	gain = new_gain;
	info("augain: new gain is %.2f\n", gain);

	return 0;
}

// COMMAND /augain ]
// COMMAND /augainch [

static int cmd_augain_channel(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = (struct cmd_arg *)arg;
	(void)pf;
	double new_gain = 0.0;

//	int channel;
	char cid[1024];
	char inout;
    if (sscanf(carg->prm, "%1023s %c %lf", cid, &inout, &new_gain) < 3) {
        re_hprintf(pf, "Usage: augainch <call_id> <e/d> <gain>\n");
        return EINVAL;
    }

	// todo: review CHANNELS_MAX
/*	int channels_max = CHANNELS_MAX;

	if (channel < 0 || channel >= channels_max) {
		warning("augain: invalid gain value %i (channels_max=%i)\n", channel, channels_max);
		return EINVAL;
	}
*/
	if (new_gain < 0.0) {
		warning("augain: invalid gain value %i\n", new_gain);
		return EINVAL;
	}

/*	
	struct augain_enc *st = (struct augain_enc *)channels[channel].p;

	if (!st) {
		warning("augain: channel=%i not found\n", channel);
		return EINVAL;
	}
*/
	// APPLY GAIN [

	double gain_enc = (inout == 'e') ? new_gain : -1;
	double gain_dec = (inout == 'd') ? new_gain : -1;

	// Apply 
	for (struct le *le = list_head(uag_list()); le; le = le->next) {
		struct ua *ua = le->data;

		struct list *calls = ua_calls(ua);

		if (!calls) {
			warning("augain: calls is null for ua %p\n", ua);
			continue;
		}
		
		struct le *ale;
		for (ale = list_head(calls); ale; ale = ale->next) {
			struct call *call = ale->data;

			if (!call) {
				warning("augain: call is null for ua %p\n", ua);
				continue;
			}

			int established = call_state(call) == CALL_STATE_ESTABLISHED;

			if (!established) {
				continue;
			}

			struct audio *au = call_audio(call);

			if (!au) {
				warning("augain: au is null for call %p\n", call);
				continue;
			}

			const char *this_id = call_id(call);
			if (str_cmp(this_id, cid)) {
				warning("augain: skip call->call_id %s != cid %s\n", this_id, cid);
				continue;
			}

			info("call au=%p established=%i\n", au, established);

			channels_apply_gain(au, gain_enc, gain_dec);
		}

		info("%p\n", ua);
	}

	// APPLY GAIN ]

/*
	if (inout == 0) {
		channels[channel].gain_enc = new_gain;
	}
	else {
		channels[channel].gain_dec = new_gain;
	}
*/
//	info("augain: new gain is %.2f channel=%i %s\n", new_gain, channel, (inout == 'e') ? "encode" : "decode");
	info("augain: new gain is %.2f channel.cid=%s %s\n", new_gain, cid, (inout == 'e') ? "encode" : "decode");

//	info("augain: channel=%i %p gain_enc=%.2f gain_dec=%.2f\n", channel, st, channels[channel].gain_enc, channels[channel].gain_dec);

	return 0;
}

// COMMAND /augainch ]
// COMMANDS [

static const struct cmd cmdv[] = {
	{"augain", 0, CMD_PRM, "Set augain <gain>", cmd_augain},
	{"augainch", 0, CMD_PRM, "Set augain for channel. augainch <channel> <inout> <gain>", cmd_augain_channel},
};

// COMMANDS ]

static int module_init(void)
{
	aufilt_register(baresip_aufiltl(), &augain);

	conf_get_float(conf_cur(), "augain", &gain);

	info("augain: gaining by at most %.2f\n", gain);

	return cmd_register(baresip_commands(), cmdv, RE_ARRAY_SIZE(cmdv));
}


static int module_close(void)
{
	aufilt_unregister(&augain);
	
	cmd_unregister(baresip_commands(), cmdv);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(augain) = {
	"augain",
	"filter",
	module_init,
	module_close
};
