/*
 *  AudioDriver_Amiga.h
 *  MilkyPlay
 *
 *  Created by neoman on 14.05.20
 *
 */
#ifndef __AUDIODRIVER_AMIGA_H__
#define __AUDIODRIVER_AMIGA_H__

#include "AudioDriverBase.h"

#define DEBUG_DRIVER            1

#define CIAAPRA                 0xbfe001

#define CUSTOM_REGBASE          0xdff000
#define CUSTOM_DMACON           (CUSTOM_REGBASE + 0x096)
#define CUSTOM_INTENA           (CUSTOM_REGBASE + 0x09a)

#define MAX_VOLUME              0x40

#define PAULA_CLK_PAL           3546895
#define PAL_LINES               312

struct Interrupt;

template<typename SampleType>
class AudioDriver_Amiga : public AudioDriverBase
{
protected:
	enum OutputMode {
		Mix,
		DirectOut,
		ResampleHW
	};

	bool                allocated;

	OutputMode          outputMode;

	mp_sint32   		idxRead, idxWrite;
	mp_uint32           chunkSize, ringSize, fetchSize;
	mp_uword    		intenaOld, dmaconOld;

	mp_sint32           statVerticalBlankMixMedian;
	mp_sint32           statAudioBufferReset, statAudioBufferResetMedian;
	mp_sint32           statRingBufferFull, statRingBufferFullMedian;
	mp_sint32           statCountPerSecond;

	mp_sword ** 		chanFetch;

	SampleType ** 		chanRingPtrs;
	SampleType ** 		chanRing;

	mp_sword * 			samplesFetched;

	SampleType * 		samplesLeft;
	SampleType * 		samplesRight;

	struct Interrupt *	irqPlayAudio;
	struct Interrupt *	irqAudioOld;
	struct Interrupt *	irqBufferAudio;

	static mp_sint32	bufferAudioService(register AudioDriver_Amiga<SampleType> * that __asm("a1"));
	static void 		playAudioService(register AudioDriver_Amiga<SampleType> * that __asm("a1"));

	mp_sint32			alloc(mp_sint32 bufferSize);
	void 				dealloc();

	mp_sint32  			bufferAudio();
	void 				playAudio();

	void      			disableIRQ();
	void        		enableIRQ();

	mp_sint32   		getStatValue(mp_uint32 key);

						AudioDriver_Amiga();
	virtual				~AudioDriver_Amiga();

	virtual void        initHardware() = 0;
	virtual void        bufferAudioImpl() = 0;
	virtual void 		playAudioImpl() = 0;

	virtual mp_uint32   getChannels() const = 0;
	virtual mp_uint32   getSampleSize() const = 0;

	virtual void		setGlobalVolume(mp_ubyte volume) = 0;
	virtual void    	disableDMA() = 0;
	virtual void      	enableDMA() = 0;

	virtual	mp_sint32	initDevice(mp_sint32 bufferSizeInWords, mp_uint32 mixFrequency, MasterMixer* mixer);
	virtual	mp_sint32	closeDevice();

	virtual	mp_sint32	start();
	virtual	mp_sint32	stop();

	virtual	mp_sint32	pause();
	virtual	mp_sint32	resume();

	virtual	const char*	getDriverID() = 0;
	virtual	mp_sint32	getPreferredBufferSize() const = 0;
};

#endif
