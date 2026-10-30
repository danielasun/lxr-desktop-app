/*
 * PunchFx.c
 *
 * Pocket Operator style punch-in effects on the master outputs.
 *
 * All the processing runs after the mixer and the FX bus on both DAC buffers.
 * A short history of the output is always recorded, the loop, reverse and tape
 * effects play back from it. Loops start on the last step boundary so the
 * first pass is identical to the live signal and the punch-in is seamless.
 *
 * There are 2 slots that can play at the same time: the time slot (repeats,
 * reverse, tape stop) feeds the tone slot (crush, filter sweeps). Performance
 * button 8 runs a master soft clip after both. While it is held, parameter
 * encoder 1 sets the clip drive and the value is kept. While crush is held, encoders
 * 1 and 2 set how long each sample is held and how many bits are kept. The clip is recorded into the
 * punch track with the other effects and played back on those steps.
 *
 * Copyright (C) 2012-2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file is part of the LXR firmware. It is licensed under the GNU General
 * Public License version 3, with the additional permission and terms in
 * NOTICE.md. See LICENSE and NOTICE.md in the root of the repository.
 */

#include "Soundengine/PunchFx.h"
#include "Soundengine/Sequencer/sequencer.h"
#include <math.h>
#include <string.h>

//history per DAC, interleaved stereo. 8192 frames = 186ms, must be a power of 2 for masking
#define PUNCH_HIST_FRAMES	8192
#define PUNCH_HIST_MASK		(PUNCH_HIST_FRAMES-1)
//longest loop that fits the history while the next blocks are still written
#define PUNCH_MAX_LOOP		(PUNCH_HIST_FRAMES - 2*OUTPUT_DMA_SIZE)

#define PUNCH_EDGE			32				//fade at loop seams [frames] to prevent clicks
#define PUNCH_XFADE_STEP	(1.f/128.f)		//dry/wet ramp per frame when an effect starts/ends
#define PUNCH_CRUSH_HOLD_MIN		1		//1 captures every frame
#define PUNCH_CRUSH_HOLD_MAX		16
#define PUNCH_CRUSH_HOLD_DEFAULT	5
#define PUNCH_CRUSH_BITS_MIN		1
#define PUNCH_CRUSH_BITS_MAX		16		//int16 sample
#define PUNCH_CRUSH_BITS_DEFAULT	5		//top 5 bits, mask 0xF800
#define PUNCH_SWEEP_STEPS	4				//filter sweep duration [steps]
#define PUNCH_SVF_DAMP		0.4f			//2 - 2*resonance, highpass sweep only

// DJ filter on the lowpass button. Cut 50 is bypass.
#define PUNCH_DJ_CUT_MIN		0
#define PUNCH_DJ_CUT_MAX		100
#define PUNCH_DJ_CUT_MID		50
#define PUNCH_DJ_RES_MIN		0
#define PUNCH_DJ_RES_MAX		100
#define PUNCH_DJ_LP_OPEN_HZ		16000.f		//lowpass just below center
#define PUNCH_DJ_LP_CLOSED_HZ	80.f		//lowpass at cut 0
#define PUNCH_DJ_HP_OPEN_HZ		40.f		//highpass just above center
#define PUNCH_DJ_HP_CLOSED_HZ	10000.f		//highpass at cut 100
#define PUNCH_DJ_Q_MIN			0.70710678f	//Butterworth, res 0
#define PUNCH_DJ_Q_MAX			8.f			//res 100
// drive is stored in tenths: 10 = gain 1.0, 40 = gain 4.0
#define PUNCH_DIST_DRIVE_MIN		10
#define PUNCH_DIST_DRIVE_MAX		40
#define PUNCH_DIST_DRIVE_DEFAULT	40

#define PUNCH_NUM_CH		4				//2 DACs * stereo

//tone effect of each step code in bits 4-5 of a punch track step
static const uint8_t punch_toneTypes[4] = {PUNCH_OFF, PUNCH_CRUSH, PUNCH_LPF, PUNCH_HPF};

typedef struct
{
	float ic1;
	float ic2;
} PunchSvf;

//one slot per effect group
typedef struct
{
	//--- written by the UI ---
	volatile uint8_t liveType;
	volatile uint8_t liveTrig;		//incremented on every press -> retrigger

	//--- audio side ---
	uint8_t lastLiveTrig;
	uint8_t restart;
	uint8_t recNext;				//pressed late in a step -> record to the next step

	uint8_t type;					//effect currently rendered
	uint8_t fadingOut;				//fade to dry before switching to a new effect
	float mix;						//0 = dry, 1 = wet
} PunchSlot;

static int16_t punch_hist[2][PUNCH_HIST_FRAMES*2];

static struct
{
	PunchSlot slot[PUNCH_NUM_GROUPS];
	volatile uint8_t erase;

	uint32_t wr;					//frames written to the history
	uint8_t frozen;					//history writing stopped to keep a captured loop

	uint8_t stepValid;
	uint8_t lastMasterStep;
	uint8_t curStep;
	uint8_t stepStartValid;
	uint32_t stepStartWr;			//history position of the last step boundary

	//loop + reverse
	uint32_t loopStart;
	uint32_t loopLen;
	uint32_t loopCaptured;			//frames of a frozen repeat in the history, kept when the rate changes
	uint32_t phase;
	uint8_t firstPass;

	//tape stop
	uint32_t rpInt;
	float rpFrac;
	float rate;
	float rateDec;

	//filter sweep
	float fc;
	float fcEnd;
	float fcMul;
	PunchSvf svf[PUNCH_NUM_CH];

	//crush
	uint8_t holdCnt;
	int16_t held[PUNCH_NUM_CH];
	uint8_t crushHold;			//frames a sample is held, 1..16
	uint8_t crushBits;			//bits kept, 1..16
	int16_t crushMask;

	//DJ filter (lowpass button). mode 0 bypass, 1 lowpass, 2 highpass
	uint8_t djCut;
	uint8_t djRes;
	volatile uint8_t djMode;
	volatile float djFc;
	volatile float djDamp;
	uint8_t djRenderedMode;		//audio thread
	float djFcSlew;				//audio thread

	uint8_t editFocus;			//PunchEditFocusEnum, last held effect that has parameters

	//perf button 8, master soft clip
	volatile uint8_t distort;		//held on the panel
	uint8_t lastDistort;
	uint8_t distortRecNext;			//pressed late in a step -> record on the next one
	uint8_t distortPlay;			//live hold, or the recorded step
	float distortMix;
	uint8_t distortDrive;			//remembered tenths, PUNCH_DIST_DRIVE_MIN..MAX
	volatile float distortGain;		//linear gain used by the sample loop
} pfx;

//------------------------------------------------------------------------------
void PunchFx_init()
{
	memset(&pfx, 0, sizeof(pfx));
	memset(punch_hist, 0, sizeof(punch_hist));
	pfx.distortDrive = PUNCH_DIST_DRIVE_DEFAULT;
	pfx.distortGain = PUNCH_DIST_DRIVE_DEFAULT / 10.f;
	pfx.crushHold = PUNCH_CRUSH_HOLD_DEFAULT;
	pfx.crushBits = PUNCH_CRUSH_BITS_DEFAULT;
	pfx.crushMask = (int16_t)((uint16_t)0xFFFF << (16 - PUNCH_CRUSH_BITS_DEFAULT));
	pfx.djCut = PUNCH_DJ_CUT_MID;
	pfx.djRes = PUNCH_DJ_RES_MIN;
	pfx.djDamp = 1.f / PUNCH_DJ_Q_MIN;
	pfx.djFc = 1000.f;
}
//------------------------------------------------------------------------------
static uint8_t punch_focusForTone(uint8_t type)
{
	if(type == PUNCH_CRUSH) return PUNCH_EDIT_CRUSH;
	if(type == PUNCH_LPF) return PUNCH_EDIT_DJ;
	return pfx.distort ? PUNCH_EDIT_DISTORT : PUNCH_EDIT_NONE;
}
//------------------------------------------------------------------------------
void PunchFx_setLive(uint8_t group, uint8_t type)
{
	if(group >= PUNCH_NUM_GROUPS) return;
	if(type >= PUNCH_NUM_TYPES || (type != PUNCH_OFF && PunchFx_getGroup(type) != group)) type = PUNCH_OFF;

	PunchSlot* s = &pfx.slot[group];
	s->liveType = type;
	if(type != PUNCH_OFF) s->liveTrig++;

	if(group == PUNCH_GROUP_TONE)
	{
		if(type == PUNCH_CRUSH || type == PUNCH_LPF)
			pfx.editFocus = punch_focusForTone(type);
		else if(pfx.editFocus == PUNCH_EDIT_CRUSH || pfx.editFocus == PUNCH_EDIT_DJ)
			pfx.editFocus = pfx.distort ? PUNCH_EDIT_DISTORT : PUNCH_EDIT_NONE;
	}
}
//------------------------------------------------------------------------------
void PunchFx_setErase(uint8_t active)
{
	pfx.erase = active;
}
//------------------------------------------------------------------------------
void PunchFx_setDistort(uint8_t active)
{
	pfx.distort = active ? 1 : 0;
	if(pfx.distort)
		pfx.editFocus = PUNCH_EDIT_DISTORT;
	else if(pfx.editFocus == PUNCH_EDIT_DISTORT)
		pfx.editFocus = punch_focusForTone(pfx.slot[PUNCH_GROUP_TONE].liveType);
}
//------------------------------------------------------------------------------
uint8_t PunchFx_distortIsHeld(void)
{
	return pfx.distort;
}
//------------------------------------------------------------------------------
uint8_t PunchFx_getDistortDrive(void)
{
	return pfx.distortDrive;
}
//------------------------------------------------------------------------------
void PunchFx_adjustDistortDrive(int8_t inc)
{
	int16_t v = (int16_t)pfx.distortDrive + inc;
	if(v < PUNCH_DIST_DRIVE_MIN) v = PUNCH_DIST_DRIVE_MIN;
	if(v > PUNCH_DIST_DRIVE_MAX) v = PUNCH_DIST_DRIVE_MAX;
	pfx.distortDrive = (uint8_t)v;
	pfx.distortGain = v / 10.f;
}
//------------------------------------------------------------------------------
uint8_t PunchFx_getEditFocus(void)
{
	return pfx.editFocus;
}
//------------------------------------------------------------------------------
uint8_t PunchFx_getCrushHold(void)
{
	return pfx.crushHold;
}
//------------------------------------------------------------------------------
void PunchFx_adjustCrushHold(int8_t inc)
{
	int16_t v = (int16_t)pfx.crushHold + inc;
	if(v < PUNCH_CRUSH_HOLD_MIN) v = PUNCH_CRUSH_HOLD_MIN;
	if(v > PUNCH_CRUSH_HOLD_MAX) v = PUNCH_CRUSH_HOLD_MAX;
	if((uint8_t)v != pfx.crushHold)
	{
		pfx.crushHold = (uint8_t)v;
		pfx.holdCnt = 0;
	}
}
//------------------------------------------------------------------------------
uint8_t PunchFx_getCrushBits(void)
{
	return pfx.crushBits;
}
//------------------------------------------------------------------------------
static int16_t punch_bitsToMask(uint8_t bits)
{
	if(bits >= 16) return (int16_t)0xFFFF;
	return (int16_t)((uint16_t)0xFFFF << (16 - bits));
}
//------------------------------------------------------------------------------
void PunchFx_adjustCrushBits(int8_t inc)
{
	int16_t v = (int16_t)pfx.crushBits + inc;
	if(v < PUNCH_CRUSH_BITS_MIN) v = PUNCH_CRUSH_BITS_MIN;
	if(v > PUNCH_CRUSH_BITS_MAX) v = PUNCH_CRUSH_BITS_MAX;
	pfx.crushBits = (uint8_t)v;
	pfx.crushMask = punch_bitsToMask(pfx.crushBits);
}
//------------------------------------------------------------------------------
static void punch_updateDj(void)
{
	uint8_t mode = 0;
	float fc = pfx.djFc;
	if(pfx.djCut < PUNCH_DJ_CUT_MID)
	{
		const float t = (float)(PUNCH_DJ_CUT_MID - pfx.djCut) / (float)PUNCH_DJ_CUT_MID;
		fc = PUNCH_DJ_LP_OPEN_HZ * powf(PUNCH_DJ_LP_CLOSED_HZ / PUNCH_DJ_LP_OPEN_HZ, t);
		mode = 1;
	}
	else if(pfx.djCut > PUNCH_DJ_CUT_MID)
	{
		const float t = (float)(pfx.djCut - PUNCH_DJ_CUT_MID) / (float)(PUNCH_DJ_CUT_MAX - PUNCH_DJ_CUT_MID);
		fc = PUNCH_DJ_HP_OPEN_HZ * powf(PUNCH_DJ_HP_CLOSED_HZ / PUNCH_DJ_HP_OPEN_HZ, t);
		mode = 2;
	}
	pfx.djFc = fc;
	pfx.djMode = mode;
}
//------------------------------------------------------------------------------
uint8_t PunchFx_getDjCut(void)
{
	return pfx.djCut;
}
//------------------------------------------------------------------------------
void PunchFx_adjustDjCut(int8_t inc)
{
	int16_t v = (int16_t)pfx.djCut + inc;
	if(v < PUNCH_DJ_CUT_MIN) v = PUNCH_DJ_CUT_MIN;
	if(v > PUNCH_DJ_CUT_MAX) v = PUNCH_DJ_CUT_MAX;
	pfx.djCut = (uint8_t)v;
	punch_updateDj();
}
//------------------------------------------------------------------------------
uint8_t PunchFx_getDjRes(void)
{
	return pfx.djRes;
}
//------------------------------------------------------------------------------
void PunchFx_adjustDjRes(int8_t inc)
{
	int16_t v = (int16_t)pfx.djRes + inc;
	if(v < PUNCH_DJ_RES_MIN) v = PUNCH_DJ_RES_MIN;
	if(v > PUNCH_DJ_RES_MAX) v = PUNCH_DJ_RES_MAX;
	pfx.djRes = (uint8_t)v;
	{
		const float q = PUNCH_DJ_Q_MIN * powf(PUNCH_DJ_Q_MAX / PUNCH_DJ_Q_MIN, pfx.djRes / 100.f);
		pfx.djDamp = 1.f / q;
	}
}
//------------------------------------------------------------------------------
static uint32_t punch_stepFrames()
{
	uint16_t bpm = seq_getBpm();
	if(!bpm) bpm = 120;
	// 60s / 4 steps per beat
	return (uint32_t)(REAL_FS * 15.f / bpm);
}
//------------------------------------------------------------------------------
static uint32_t punch_clampLen(uint32_t len)
{
	//halving keeps the loop in sync at slow tempos
	while(len > PUNCH_MAX_LOOP) len >>= 1;
	if(len < 4*PUNCH_EDGE) len = 4*PUNCH_EDGE;
	return len;
}
//------------------------------------------------------------------------------
// the recorded effect of a group on a punch track step
static uint8_t punch_decodeStep(uint8_t step, uint8_t group)
{
	if(group == PUNCH_GROUP_TONE)
		return punch_toneTypes[(step & PUNCH_STEP_TONE_MASK) >> PUNCH_STEP_TONE_SHIFT];
	const uint8_t type = step & PUNCH_STEP_TIME_MASK;
	if(type >= PUNCH_NUM_TYPES || PunchFx_getGroup(type) != group) return PUNCH_OFF;
	return type;
}
//------------------------------------------------------------------------------
// the step bits of a tone effect
static uint8_t punch_encodeTone(uint8_t type)
{
	for(uint8_t i=1;i<4;i++)
	{
		if(punch_toneTypes[i] == type) return (uint8_t)(i << PUNCH_STEP_TONE_SHIFT);
	}
	return 0;
}
//------------------------------------------------------------------------------
static uint32_t punch_repeatLen(uint8_t type, uint32_t stepFrames)
{
	switch(type)
	{
	case PUNCH_LOOP8:	return stepFrames*2;
	case PUNCH_LOOP12:	return stepFrames*4/3;
	case PUNCH_LOOP16T:	return stepFrames*2/3;
	case PUNCH_LOOP32:	return stepFrames/2;
	case PUNCH_LOOP48:	return stepFrames/3;
	case PUNCH_LOOP64:	return stepFrames/4;
	default:			return stepFrames;
	}
}
//------------------------------------------------------------------------------
// livePos is the history position of the first frame of the current block
static void punch_startTime(uint8_t type, uint32_t livePos)
{
	PunchSlot* s = &pfx.slot[PUNCH_GROUP_TIME];
	const uint32_t stepFrames = punch_stepFrames();

	//a new rate while a repeat plays: keep repeating the captured audio if it is long enough,
	//like a roll. A longer rate than the capture needs fresh audio and starts over,
	//and a retrigger of the same rate captures again
	if(PunchFx_isRepeat(type) && PunchFx_isRepeat(s->type) && type != s->type)
	{
		const uint32_t len = punch_clampLen(punch_repeatLen(type, stepFrames));
		const uint32_t captured = pfx.frozen ? pfx.loopCaptured : livePos - pfx.loopStart;
		if(len <= captured)
		{
			s->type = type;
			s->fadingOut = 0;
			s->restart = 0;
			pfx.loopLen = len;
			pfx.loopCaptured = captured;
			pfx.phase = 0;
			pfx.firstPass = 0;
			pfx.frozen = 1;
			return;
		}
	}

	if(pfx.frozen)
	{
		//history has a gap now
		pfx.frozen = 0;
		pfx.stepStartValid = 0;
	}

	s->type = type;
	s->fadingOut = 0;
	s->restart = 0;

	switch(type)
	{
	case PUNCH_LOOP8:
	case PUNCH_LOOP12:
	case PUNCH_LOOP16:
	case PUNCH_LOOP16T:
	case PUNCH_LOOP32:
	case PUNCH_LOOP48:
	case PUNCH_LOOP64:
	{
		const uint32_t len = punch_clampLen(punch_repeatLen(type, stepFrames));

		//align the loop to the grid if the step start is still in the history
		uint32_t base = livePos;
		if(pfx.stepStartValid && (livePos - pfx.stepStartWr) < PUNCH_MAX_LOOP)
		{
			base = pfx.stepStartWr;
		}
		pfx.loopStart = base + ((livePos - base) / len) * len;
		pfx.phase = livePos - pfx.loopStart;
		pfx.loopLen = len;
		pfx.firstPass = 1;
	}
		break;

	case PUNCH_REVERSE:
	{
		uint32_t len = punch_clampLen(stepFrames*2);
		if(len > livePos) len = livePos; //not enough history yet after boot
		if(len < 4*PUNCH_EDGE)
		{
			s->type = PUNCH_OFF;
			break;
		}
		//play [livePos-len, livePos) backwards, the first output frame continues from the last live frame
		pfx.loopStart = livePos - len;
		pfx.loopLen = len;
		pfx.phase = 0;
		pfx.firstPass = 1;
		pfx.frozen = 1;
	}
		break;

	case PUNCH_TAPESTOP:
	{
		//the read pointer lags behind by half the stop time, which must fit the history
		uint32_t stopFrames = stepFrames*4;
		if(stopFrames > 2*(PUNCH_MAX_LOOP - PUNCH_EDGE)) stopFrames = 2*(PUNCH_MAX_LOOP - PUNCH_EDGE);
		pfx.rpInt = livePos;
		pfx.rpFrac = 0.f;
		pfx.rate = 1.f;
		pfx.rateDec = 1.f/stopFrames;
	}
		break;

	default:
		s->type = PUNCH_OFF;
		break;
	}
}
//------------------------------------------------------------------------------
static void punch_startSweep(float start, float end, uint32_t frames)
{
	const float sweepFrames = (float)(punch_stepFrames() * PUNCH_SWEEP_STEPS);
	pfx.fc = start;
	pfx.fcEnd = end;
	pfx.fcMul = powf(end/start, frames/sweepFrames);
	memset(pfx.svf, 0, sizeof(pfx.svf));
}
//------------------------------------------------------------------------------
static void punch_startTone(uint8_t type, uint32_t frames)
{
	PunchSlot* s = &pfx.slot[PUNCH_GROUP_TONE];

	s->type = type;
	s->fadingOut = 0;
	s->restart = 0;

	switch(type)
	{
	case PUNCH_CRUSH:
		pfx.holdCnt = 0;
		break;

	case PUNCH_LPF:
		memset(pfx.svf, 0, sizeof(pfx.svf));
		pfx.djRenderedMode = 0;
		pfx.djFcSlew = pfx.djFc;
		break;

	case PUNCH_HPF:
		punch_startSweep(400.f, 2500.f, frames);
		break;

	default:
		s->type = PUNCH_OFF;
		break;
	}
}
//------------------------------------------------------------------------------
// called once per block before rendering: follows the sequencer steps,
// records/erases the punch track and decides which effects should play
static void punch_handleSteps(uint32_t frames)
{
	uint8_t* track = seq_patternSet.punchFx[seq_activePattern];
	uint8_t live[PUNCH_NUM_GROUPS];
	uint8_t pressed[PUNCH_NUM_GROUPS];
	const uint8_t liveDistort = pfx.distort;
	const uint8_t distortPressed = liveDistort && !pfx.lastDistort;
	pfx.lastDistort = liveDistort;

	for(uint8_t g=0;g<PUNCH_NUM_GROUPS;g++)
	{
		PunchSlot* s = &pfx.slot[g];
		live[g] = s->liveType;
		pressed[g] = s->liveTrig != s->lastLiveTrig;
		if(pressed[g])
		{
			s->lastLiveTrig = s->liveTrig;
			s->restart = 1;
		}
	}

	if(seq_isRunning())
	{
		const uint8_t m = seq_getMasterStepCnt();
		//master step count 0 => the sequencer was started but has not played the first step yet
		if(pfx.stepValid ? (m != pfx.lastMasterStep) : (m != 0))
		{
			pfx.lastMasterStep = m;
			pfx.curStep = seq_getStepIndex(seq_getLongestTrack()) % PUNCH_STEPS;
			pfx.stepValid = 1;
			pfx.stepStartWr = pfx.wr;
			pfx.stepStartValid = !pfx.frozen;

			if(pfx.erase)
			{
				track[pfx.curStep] = PUNCH_OFF;
			}
			//overdub: a held group replaces only its own part of the step
			uint8_t timeFx = punch_decodeStep(track[pfx.curStep], PUNCH_GROUP_TIME);
			uint8_t toneFx = punch_decodeStep(track[pfx.curStep], PUNCH_GROUP_TONE);
			uint8_t dist = track[pfx.curStep] & PUNCH_STEP_DISTORT;
			uint8_t changed = 0;
			for(uint8_t g=0;g<PUNCH_NUM_GROUPS;g++)
			{
				PunchSlot* s = &pfx.slot[g];
				const uint8_t rec = live[g] != PUNCH_OFF ? live[g] : s->recNext;
				if(seq_recordActive && rec != PUNCH_OFF)
				{
					if(g == PUNCH_GROUP_TIME) timeFx = rec;
					else toneFx = rec;
					changed = 1;
				}
				s->recNext = PUNCH_OFF;
			}
			if(seq_recordActive && (liveDistort || pfx.distortRecNext))
			{
				dist = PUNCH_STEP_DISTORT;
				changed = 1;
			}
			pfx.distortRecNext = 0;
			if(changed)
			{
				track[pfx.curStep] = (uint8_t)(timeFx | punch_encodeTone(toneFx) | dist);
			}
		}

		//quantise a press to the nearest step like the note recording does
		for(uint8_t g=0;g<PUNCH_NUM_GROUPS;g++)
		{
			if(pressed[g] && pfx.stepValid && seq_recordActive && live[g] != PUNCH_OFF)
			{
				if(seq_getElapsedStepTimePercent() < 50.f)
				{
					uint8_t* step = &track[pfx.curStep];
					if(g == PUNCH_GROUP_TIME)
						*step = (uint8_t)((*step & ~PUNCH_STEP_TIME_MASK) | live[g]);
					else
						*step = (uint8_t)((*step & ~PUNCH_STEP_TONE_MASK) | punch_encodeTone(live[g]));
				}
				else
				{
					pfx.slot[g].recNext = live[g];
				}
			}
		}
		if(distortPressed && pfx.stepValid && seq_recordActive)
		{
			if(seq_getElapsedStepTimePercent() < 50.f)
				track[pfx.curStep] |= PUNCH_STEP_DISTORT;
			else
				pfx.distortRecNext = 1;
		}
	}
	else
	{
		pfx.stepValid = 0;
		pfx.stepStartValid = 0;
		pfx.slot[PUNCH_GROUP_TIME].recNext = PUNCH_OFF;
		pfx.slot[PUNCH_GROUP_TONE].recNext = PUNCH_OFF;
		pfx.distortRecNext = 0;
	}

	for(uint8_t g=0;g<PUNCH_NUM_GROUPS;g++)
	{
		PunchSlot* s = &pfx.slot[g];

		//live play overrides the recorded track
		uint8_t want = live[g];
		if(want == PUNCH_OFF && pfx.stepValid)
		{
			want = punch_decodeStep(track[pfx.curStep], g);
		}

		if(want != s->type || s->restart)
		{
			if(s->type == PUNCH_OFF || s->mix <= 0.f)
			{
				if(g == PUNCH_GROUP_TIME) punch_startTime(want, pfx.wr);
				else punch_startTone(want, frames);
			}
			else
			{
				s->fadingOut = 1;
			}
		}
		else
		{
			s->fadingOut = 0;
		}
	}

	//held button overrides the track, otherwise the recorded steps play it back
	uint8_t wantDistort = liveDistort;
	if(!wantDistort && pfx.stepValid && (track[pfx.curStep] & PUNCH_STEP_DISTORT))
		wantDistort = 1;
	pfx.distortPlay = wantDistort;
}
//------------------------------------------------------------------------------
static inline int16_t punch_read(uint8_t ch, uint32_t pos)
{
	return punch_hist[ch>>1][((pos & PUNCH_HIST_MASK)<<1) + (ch&1)];
}
//------------------------------------------------------------------------------
// fade in/out at the seams of a loop, the first pass is the live signal and is not faded in
static inline float punch_edgeGain(uint32_t phase, uint32_t len, uint8_t firstPass)
{
	float g = 1.f;
	if(!firstPass && phase < PUNCH_EDGE) g = phase * (1.f/PUNCH_EDGE);
	if(len - phase < PUNCH_EDGE)
	{
		const float g2 = (len - phase) * (1.f/PUNCH_EDGE);
		if(g2 < g) g = g2;
	}
	return g;
}
//------------------------------------------------------------------------------
static inline void punch_rampMix(PunchSlot* s)
{
	const float target = (s->fadingOut || s->type == PUNCH_OFF) ? 0.f : 1.f;
	if(s->mix < target)
	{
		s->mix += PUNCH_XFADE_STEP;
		if(s->mix > target) s->mix = target;
	}
	else if(s->mix > target)
	{
		s->mix -= PUNCH_XFADE_STEP;
		if(s->mix < target) s->mix = target;
	}
}
//------------------------------------------------------------------------------
void PunchFx_process(int16_t* out1, int16_t* out2, uint32_t frames)
{
	int16_t* outs[2] = {out1, out2};
	PunchSlot* timeSlot = &pfx.slot[PUNCH_GROUP_TIME];
	PunchSlot* toneSlot = &pfx.slot[PUNCH_GROUP_TONE];

	punch_handleSteps(frames);

	//--- record the history ---
	if(!pfx.frozen)
	{
		for(uint8_t p=0;p<2;p++)
		{
			uint32_t idx = (pfx.wr & PUNCH_HIST_MASK)<<1;
			for(uint32_t i=0;i<frames*2;i++)
			{
				punch_hist[p][idx] = outs[p][i];
				idx = (idx+1) & ((PUNCH_HIST_FRAMES<<1)-1);
			}
		}
		pfx.wr += frames;
	}

	const uint8_t timeType = timeSlot->type;
	const uint8_t toneType = toneSlot->type;

	if(timeType == PUNCH_OFF && timeSlot->mix <= 0.f && toneType == PUNCH_OFF && toneSlot->mix <= 0.f
			&& !pfx.distortPlay && pfx.distortMix <= 0.f)
	{
		return;
	}

	//--- per block filter coefficients (2-pole TPT state variable filter) ---
	float a1=0, a2=0, a3=0;
	float filtK = PUNCH_SVF_DAMP;
	uint8_t djMode = 0;
	if(toneType == PUNCH_LPF)
	{
		djMode = pfx.djMode;
		if(djMode != pfx.djRenderedMode)
		{
			memset(pfx.svf, 0, sizeof(pfx.svf));
			pfx.djRenderedMode = djMode;
			pfx.djFcSlew = pfx.djFc;
		}
		if(djMode)
		{
			pfx.djFcSlew += (pfx.djFc - pfx.djFcSlew) * 0.4f;
			filtK = pfx.djDamp;
			const float g = tanf(3.14159265f * pfx.djFcSlew / REAL_FS);
			a1 = 1.f/(1.f + g*(g + filtK));
			a2 = g*a1;
			a3 = g*a2;
		}
	}
	else if(toneType == PUNCH_HPF)
	{
		const float g = tanf(3.14159265f * pfx.fc / REAL_FS);
		a1 = 1.f/(1.f + g*(g + PUNCH_SVF_DAMP));
		a2 = g*a1;
		a3 = g*a2;

		pfx.fc *= pfx.fcMul;
		if( (pfx.fcMul < 1.f && pfx.fc < pfx.fcEnd) || (pfx.fcMul > 1.f && pfx.fc > pfx.fcEnd) )
		{
			pfx.fc = pfx.fcEnd;
		}
	}

	for(uint32_t i=0;i<frames;i++)
	{
		punch_rampMix(timeSlot);
		punch_rampMix(toneSlot);

		//--- per frame state ---
		uint32_t pos = 0;
		float gain = 1.f;
		float frac = 0.f;
		uint8_t capture = 0;

		switch(timeType)
		{
		case PUNCH_LOOP8:
		case PUNCH_LOOP12:
		case PUNCH_LOOP16:
		case PUNCH_LOOP16T:
		case PUNCH_LOOP32:
		case PUNCH_LOOP48:
		case PUNCH_LOOP64:
		case PUNCH_REVERSE:
			if(timeType == PUNCH_REVERSE)
				pos = pfx.loopStart + pfx.loopLen - 1 - pfx.phase;
			else
				pos = pfx.loopStart + pfx.phase;

			gain = punch_edgeGain(pfx.phase, pfx.loopLen, pfx.firstPass);
			if(++pfx.phase >= pfx.loopLen)
			{
				pfx.phase = 0;
				pfx.firstPass = 0;
			}
			break;

		case PUNCH_TAPESTOP:
			pos = pfx.rpInt;
			frac = pfx.rpFrac;
			//fade out the last part of the slow down
			gain = pfx.rate * 5.f;
			if(gain > 1.f) gain = 1.f;

			pfx.rpFrac += pfx.rate;
			while(pfx.rpFrac >= 1.f)
			{
				pfx.rpFrac -= 1.f;
				pfx.rpInt++;
			}
			pfx.rate -= pfx.rateDec;
			if(pfx.rate < 0.f) pfx.rate = 0.f;
			break;

		default:
			break;
		}

		if(toneType == PUNCH_CRUSH)
		{
			capture = pfx.holdCnt == 0;
			if(++pfx.holdCnt >= pfx.crushHold) pfx.holdCnt = 0;
		}

		//--- render all 4 channels ---
		for(uint8_t ch=0;ch<PUNCH_NUM_CH;ch++)
		{
			int16_t* o = &outs[ch>>1][i*2 + (ch&1)];
			const float dry = *o;

			//time slot
			float wet = dry;
			switch(timeType)
			{
			case PUNCH_LOOP8:
			case PUNCH_LOOP12:
			case PUNCH_LOOP16:
			case PUNCH_LOOP16T:
			case PUNCH_LOOP32:
			case PUNCH_LOOP48:
			case PUNCH_LOOP64:
			case PUNCH_REVERSE:
				wet = punch_read(ch, pos) * gain;
				break;

			case PUNCH_TAPESTOP:
			{
				const float s0 = punch_read(ch, pos);
				const float s1 = punch_read(ch, pos+1);
				wet = (s0 + (s1 - s0)*frac) * gain;
			}
				break;

			default:
				break;
			}
			const float x = dry + (wet - dry)*timeSlot->mix;

			//tone slot, processes the time slot output
			float wet2 = x;
			switch(toneType)
			{
			case PUNCH_CRUSH:
				if(capture) pfx.held[ch] = (int16_t)__SSAT((int32_t)x, 16) & pfx.crushMask;
				wet2 = pfx.held[ch];
				break;

			case PUNCH_LPF:
				if(djMode)
				{
					PunchSvf* f = &pfx.svf[ch];
					const float v3 = x - f->ic2;
					const float v1 = a1*f->ic1 + a2*v3;
					const float v2 = f->ic2 + a2*f->ic1 + a3*v3;
					f->ic1 = 2.f*v1 - f->ic1;
					f->ic2 = 2.f*v2 - f->ic2;
					wet2 = djMode == 1 ? v2 : x - filtK*v1 - v2;
				}
				break;

			case PUNCH_HPF:
			{
				PunchSvf* f = &pfx.svf[ch];
				const float v3 = x - f->ic2;
				const float v1 = a1*f->ic1 + a2*v3;
				const float v2 = f->ic2 + a2*f->ic1 + a3*v3;
				f->ic1 = 2.f*v1 - f->ic1;
				f->ic2 = 2.f*v2 - f->ic2;
				wet2 = x - PUNCH_SVF_DAMP*v1 - v2;
			}
				break;

			default:
				break;
			}

			*o = (int16_t)__SSAT((int32_t)(x + (wet2 - x)*toneSlot->mix), 16);
		}
	}

	//master soft clip, after the punch slots so a held filter or loop is distorted too
	if(pfx.distortPlay || pfx.distortMix > 0.f)
	{
		for(uint32_t i=0;i<frames;i++)
		{
			const float target = pfx.distortPlay ? 1.f : 0.f;
			if(pfx.distortMix < target)
			{
				pfx.distortMix += PUNCH_XFADE_STEP;
				if(pfx.distortMix > target) pfx.distortMix = target;
			}
			else if(pfx.distortMix > target)
			{
				pfx.distortMix -= PUNCH_XFADE_STEP;
				if(pfx.distortMix < target) pfx.distortMix = target;
			}
			if(pfx.distortMix <= 0.f) continue;

			for(uint8_t ch=0;ch<PUNCH_NUM_CH;ch++)
			{
				int16_t* o = &outs[ch>>1][i*2 + (ch&1)];
				const float dry = *o;
				float x = dry * (pfx.distortGain/32768.f);
				x = x/(1.f + fabsf(x));
				const float wet = x*32767.f;
				*o = (int16_t)__SSAT((int32_t)(dry + (wet - dry)*pfx.distortMix), 16);
			}
		}
	}

	//a loop is fully captured once the writer passed its end -> keep it from being overwritten
	if(!pfx.frozen && PunchFx_isRepeat(timeType)
			&& (pfx.wr - pfx.loopStart) >= pfx.loopLen)
	{
		pfx.frozen = 1;
		pfx.loopCaptured = pfx.loopLen;
	}
}
