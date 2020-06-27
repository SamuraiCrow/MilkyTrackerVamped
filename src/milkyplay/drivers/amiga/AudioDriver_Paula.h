/*
 *  AudioDriver_Paula.h
 *  MilkyPlay
 *
 *  Created by neoman on 14.05.20
 *
 */
#ifndef __AUDIODRIVER_PAULA_H__
#define __AUDIODRIVER_PAULA_H__

#include "AudioDriver_Amiga.h"

class AudioDriver_Paula : public AudioDriver_Amiga<mp_sbyte>
{
protected:
	mp_ubyte			hwOldTimerLo, hwOldTimerHi;
	mp_uword			hwDMACON;
	mp_uint32			hwChannelLoopStart[4];
	mp_uword			hwChannelRepeatLength[4];
	mp_sint32           hwChannelPos[4];
	mp_sint32          	hwPeriod[4];
	struct Interrupt *	irqChannelPlayback;

	virtual void        initHardware();
	virtual void        bufferAudioImpl();
	virtual void 		playAudioImpl();

	virtual mp_uint32   getChannels() const;
	virtual mp_uint32   getSampleSize() const;

	virtual void		setGlobalVolume(mp_ubyte volume);
	virtual void    	disableDMA();
	virtual void      	enableDMA();
	virtual void    	disableIRQ();
	virtual void      	enableIRQ();
public:
						AudioDriver_Paula();
	virtual				~AudioDriver_Paula();

	mp_sint32           channelPlayback();

	virtual	mp_sint32	initDevice(mp_sint32 bufferSizeInWords, mp_uint32 mixFrequency, MasterMixer* mixer);

	virtual	const char*	getDriverID();
	virtual	mp_sint32	getPreferredBufferSize() const;

	virtual mp_sint32  	bufferAudio();

    virtual void 		setChannelFrequency(ChannelMixer::TMixerChannel * chn);
    virtual void 		setChannelVolume(ChannelMixer::TMixerChannel * chn);
    virtual void 		playSample(ChannelMixer::TMixerChannel * chn);
    virtual void 		stopSample(ChannelMixer::TMixerChannel * chn);
	virtual void 		tickDone();
};

#endif
