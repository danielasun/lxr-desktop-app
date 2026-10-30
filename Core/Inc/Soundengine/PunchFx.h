/*
 * PunchFx.h
 *
 * Pocket Operator style punch-in effects on the master outputs.
 * Played live from the step buttons in perf mode and recordable
 * into a 16 step punch track per pattern.
 * Button 8 is a 1/16 repeat. While it is held, buttons 1-7 set the repeat to 1/8, 1/12, 1/16, 1/24, 1/32, 1/48 or 1/64.
 * Button 11 is not an effect: it transposes every track by an octave.
 *
 * The effects are split in 2 groups that can be stacked:
 * one time effect (repeat, reverse, tape stop) followed by one tone effect (crush, filters).
 * Step button 9 adds a master soft clip on top of both, recorded in the punch track.
 * While that button is held, parameter encoder 1 sets the clip drive. The value is kept.
 * While the crush button is held, encoders 1 and 2 set the sample-hold and the bit depth.
 * While the lowpass button is held, encoders 1 and 2 set the DJ-filter cutoff and resonance.
 *
 * Copyright (C) 2012-2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file is part of the LXR firmware. It is licensed under the GNU General
 * Public License version 3, with the additional permission and terms in
 * NOTICE.md. See LICENSE and NOTICE.md in the root of the repository.
 */

#ifndef INC_SOUNDENGINE_PUNCHFX_H_
#define INC_SOUNDENGINE_PUNCHFX_H_

#include "Soundengine/globals.h"
#include "Soundengine/config.h"

//number of steps in the punch track of a pattern
#define PUNCH_STEPS 16

//recorded type ids. Button 8 plays LOOP16, buttons 1-7 change the rate of a held button 8.
//Button 11 is the octave transpose and is not one of these types.
//must stay below 16, SEQ_PUNCH_FX carries the type in 4 bits
enum PunchFxTypeEnum
{
	PUNCH_OFF,
	PUNCH_LOOP16,		//repeat 1/16
	PUNCH_LOOP16T,		//repeat 1/16 triplet (1/24)
	PUNCH_LOOP32,		//repeat 1/32
	PUNCH_CRUSH,		//bit crush + sample rate reduction
	PUNCH_REVERSE,		//play the last 1/8 backwards
	PUNCH_TAPESTOP,		//slow down to a halt
	PUNCH_LPF,			//DJ filter: lowpass below center, bypass at center, highpass above
	PUNCH_HPF,			//high pass sweep up
	PUNCH_LOOP8,		//repeat 1/8
	PUNCH_LOOP12,		//repeat 1/8 triplet (1/12)
	PUNCH_LOOP48,		//repeat 1/32 triplet (1/48)
	PUNCH_LOOP64,		//repeat 1/64

	PUNCH_NUM_TYPES
};

//repeat rates of buttons 1-7 while button 8 is held
#define PUNCH_NUM_RATES 7

enum PunchFxGroupEnum
{
	PUNCH_GROUP_TIME,	//plays back from the history, one at a time
	PUNCH_GROUP_TONE,	//processes the output of the time group

	PUNCH_NUM_GROUPS
};

//a punch track step holds the time effect type in bits 0-3, the tone effect in bits 4-5
//(0 off, 1 crush, 2 DJ filter, 3 highpass sweep) and the distortion flag in bit 7
#define PUNCH_STEP_TIME_MASK	0x0f
#define PUNCH_STEP_TONE_SHIFT	4
#define PUNCH_STEP_TONE_MASK	0x30
#define PUNCH_STEP_DISTORT		0x80

//SEQ_PUNCH_FX data2: bit 4 = group, bits 0-3 = type (PUNCH_OFF releases the group)
#define PUNCH_MSG(group, type)	(((group) << 4) | (type))

static inline uint8_t PunchFx_getGroup(uint8_t type)
{
	return (type == PUNCH_CRUSH || type == PUNCH_LPF || type == PUNCH_HPF) ? PUNCH_GROUP_TONE : PUNCH_GROUP_TIME;
}

static inline uint8_t PunchFx_isRepeat(uint8_t type)
{
	return type == PUNCH_LOOP8 || type == PUNCH_LOOP12 || type == PUNCH_LOOP16 || type == PUNCH_LOOP16T
			|| type == PUNCH_LOOP32 || type == PUNCH_LOOP48 || type == PUNCH_LOOP64;
}

void PunchFx_init();

/** set the effect of a group played from the front panel. PUNCH_OFF releases the group.
 * called from the UI side, every call with a type != PUNCH_OFF retriggers the effect */
void PunchFx_setLive(uint8_t group, uint8_t type);

/** 1: clear the punch track steps while the sequencer passes them */
void PunchFx_setErase(uint8_t active);

/** 1: master soft clip while performance button 9 is held */
void PunchFx_setDistort(uint8_t active);

/** 1 while performance button 9 is held */
uint8_t PunchFx_distortIsHeld(void);

/** remembered soft-clip drive in tenths, 10 = 1.0 through 40 = 4.0 */
uint8_t PunchFx_getDistortDrive(void);

/** move the remembered drive by inc encoder detents */
void PunchFx_adjustDistortDrive(int8_t inc);

enum PunchEditFocusEnum
{
	PUNCH_EDIT_NONE = 0,
	PUNCH_EDIT_DISTORT,
	PUNCH_EDIT_CRUSH,
	PUNCH_EDIT_DJ
};

/** which held punch button currently owns the parameter encoders */
uint8_t PunchFx_getEditFocus(void);

/** sample-hold length in frames. 1 captures every frame, 16 holds for 16 frames */
uint8_t PunchFx_getCrushHold(void);
void PunchFx_adjustCrushHold(int8_t inc);

/** bits kept from the 16-bit sample. 16 is full resolution, 1 keeps only the sign */
uint8_t PunchFx_getCrushBits(void);
void PunchFx_adjustCrushBits(int8_t inc);

/** DJ-filter cutoff, 0..100. 50 is bypass, below 50 is lowpass, above 50 is highpass */
uint8_t PunchFx_getDjCut(void);
void PunchFx_adjustDjCut(int8_t inc);

/** DJ-filter resonance, 0..100. 0 is Butterworth, 100 is a strong peak */
uint8_t PunchFx_getDjRes(void);
void PunchFx_adjustDjRes(int8_t inc);

/** process both interleaved stereo DAC buffers in place, after the mixer and FX bus */
void PunchFx_process(int16_t* out1, int16_t* out2, uint32_t frames);

#endif /* INC_SOUNDENGINE_PUNCHFX_H_ */
