/*
 *  AudioDriver_SAGA.h
 *  MilkyPlay
 *
 *  Created by neoman on 14.05.20
 *
 */
#ifndef __AUDIODRIVER_SAGA_H__
#define __AUDIODRIVER_SAGA_H__

#include <exec/exec.h>
#include <dos/dos.h>
#if defined(__SASC) || defined(WARPOS)
#include <proto/exec.h>
#else
#include <inline/exec.h>
#endif
#include <stdlib.h>
#include <string.h>

#include "AudioDriver_COMPENSATE.h"

class AudioDriver_SAGA : public AudioDriverBase
{
private:
	mp_ubyte  *	zero;
	mp_sint32   idxRead, idxWrite;
	mp_uword    intenaOld, dmaconOld;
	bool        directOut;

	mp_sbyte ** chanRing;
	mp_sbyte ** chanRingPtrs;
	mp_sword  * chanPan;

	mp_sword  * samplesBounced;
	mp_sbyte  * samplesLeft;
	mp_sbyte  * samplesRight;

	struct Interrupt * irqPlayAudio;
	struct Interrupt * irqAudioOld;
	struct Interrupt * irqBufferAudio;

	static mp_sint32 bufferAudioService(register AudioDriver_SAGA * that __asm("a1"));
	static void 	 playAudioService(register AudioDriver_SAGA * that __asm("a1"));

	mp_sint32  	bufferAudio();
	void 		playAudio();

	void		setGlobalVolume(mp_ubyte volume);
	void        disableDMA();
	void        enableDMA();
	void        disableIRQ();
	void        enableIRQ();
public:
				AudioDriver_SAGA();
	virtual		~AudioDriver_SAGA();

	virtual		mp_sint32	initDevice(mp_sint32 bufferSizeInWords, mp_uint32 mixFrequency, MasterMixer* mixer);
	virtual		mp_sint32	closeDevice();

	virtual		mp_sint32	start();
	virtual		mp_sint32	stop();

	virtual		mp_sint32	pause();
	virtual		mp_sint32	resume();

	virtual		const char*	getDriverID();
	virtual		mp_sint32	getPreferredBufferSize() const;
};

#endif
