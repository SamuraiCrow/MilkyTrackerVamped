/*
 *  AudioDriver_PAMELA.h
 *  MilkyPlay
 *
 *  Created by neoman on 14.05.20
 *
 */
#ifndef __AUDIODRIVER_PAMELA_H__
#define __AUDIODRIVER_PAMELA_H__

#include <exec/exec.h>
#include <dos/dos.h>
#if defined(__SASC) || defined(WARPOS)
#include <proto/exec.h>
#else
#include <inline/exec.h>
#endif
#include <stdlib.h>
#include <string.h>

#include "AudioDriverBase.h"

#define PAULA

#ifdef PAULA
#   define MAX_BANKS 1
typedef mp_sbyte mp_smptype;
#else
#   define MAX_BANKS 2
typedef mp_sword mp_smptype;
#endif
#define SAMPLE_SIZE sizeof(mp_smptype)

#define MAX_CHANNELS (MAX_BANKS << 2)

class AudioDriver_Pamela : public AudioDriverBase
{
private:
	mp_sint32   		idxRead, idxWrite;
	mp_uword    		intenaOld, dmaconOld;
	bool      			directOut;

	mp_sword ** 		chanFetch;

	mp_smptype ** 		chanRingPtrs;
	mp_smptype ** 		chanRing;

	mp_sword * 			samplesFetched;

	mp_smptype * 		samplesLeft;
	mp_smptype * 		samplesRight;

	struct Interrupt *	irqPlayAudio;
	struct Interrupt *	irqAudioOld;
	struct Interrupt *	irqBufferAudio;

	static mp_sint32	bufferAudioService(register AudioDriver_Pamela * that __asm("a1"));
	static void 		playAudioService(register AudioDriver_Pamela * that __asm("a1"));

	mp_sint32  			bufferAudio();
	void 				playAudio();

	void				setGlobalVolume(mp_ubyte volume);
	void        		disableDMA();
	void        		enableDMA();
	void        		disableIRQ();
	void        		enableIRQ();
public:
						AudioDriver_Pamela();
	virtual				~AudioDriver_Pamela();

	virtual				mp_sint32	initDevice(mp_sint32 bufferSizeInWords, mp_uint32 mixFrequency, MasterMixer* mixer);
	virtual				mp_sint32	closeDevice();

	virtual				mp_sint32	start();
	virtual				mp_sint32	stop();

	virtual				mp_sint32	pause();
	virtual				mp_sint32	resume();

	virtual				const char*	getDriverID();
	virtual				mp_sint32	getPreferredBufferSize() const;
};

#endif
