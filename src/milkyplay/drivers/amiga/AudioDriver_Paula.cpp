#include "AudioDriver_Paula.h"
#include "MasterMixer.h"


#define AUDIO_REGBASE(CH)       (CUSTOM_REGBASE + 0x0a0 + ((CH & 0x3) << 4))
#define AUDIO_REG(CH, IDX)      (AUDIO_REGBASE(CH) + (IDX))

#define AUDIO_LOCHI(CH)         AUDIO_REG(CH, 0x00)
#define AUDIO_LOCLO(CH)         AUDIO_REG(CH, 0x02)
#define AUDIO_LENGTH(CH)        AUDIO_REG(CH, 0x04)
#define AUDIO_PERIOD(CH)        AUDIO_REG(CH, 0x06)
#define AUDIO_VOLUME(CH)        AUDIO_REG(CH, 0x08)

#define MAX_CHANNELS            4

#define SAMPLE_SIZE             MP_NUMBYTES

static mp_sint32
channelPlaybackService(register AudioDriver_Paula * that __asm("a1"))
{
    return that->channelPlayback();
}

AudioDriver_Paula::AudioDriver_Paula()
: hwOldTimerLo(0)
, hwOldTimerHi(0)
, hwDMACON(0)
{
    for(int i = 0; i < 4; i++) {
        hwChannelLoopStart[i] = 0;
        hwChannelRepeatLength[i] = 1;
    }
}

AudioDriver_Paula::~AudioDriver_Paula()
{
}

mp_uint32
AudioDriver_Paula::getChannels() const
{
    return MAX_CHANNELS;
}

mp_uint32
AudioDriver_Paula::getSampleSize() const
{
    return SAMPLE_SIZE;
}

mp_sint32
AudioDriver_Paula::initDevice(mp_sint32 bufferSizeInWords, mp_uint32 mixFrequency, MasterMixer* mixer)
{
#if DEBUG_DRIVER
    printf("%s\n", __PRETTY_FUNCTION__);
    printf("Wanted mix frequency: %ld\n", mixFrequency);
    printf("Driver running in Paula mode\n");
#endif

    mixFrequency = 22050;

    irqChannelPlayback = (struct Interrupt *) AllocMem(sizeof(struct Interrupt), MEMF_PUBLIC | MEMF_CLEAR);
    irqChannelPlayback->is_Node.ln_Type = NT_INTERRUPT;
    irqChannelPlayback->is_Node.ln_Pri = 127;
    irqChannelPlayback->is_Node.ln_Name = (char *) "mt-paula-chn-pb";
    irqChannelPlayback->is_Data = this;
    irqChannelPlayback->is_Code = (void(*)()) channelPlaybackService;

    return AudioDriver_Amiga::initDevice(bufferSizeInWords, mixFrequency, mixer);
}

void
AudioDriver_Paula::setGlobalVolume(mp_ubyte volume)
{
    int i;

    switch(outputMode) {
    case Mix:
    case DirectOut:
        for(i = 0; i < MAX_CHANNELS; i++) {
            *((volatile mp_uword *) AUDIO_VOLUME(i)) = volume;
        }
        break;
    }
}

void
AudioDriver_Paula::disableDMA()
{
    switch(outputMode) {
    case Mix:
        *((volatile mp_uword *) CUSTOM_DMACON) = DMAF_AUD0 | DMAF_AUD1;
        break;
    case DirectOut:
        *((volatile mp_uword *) CUSTOM_DMACON) = DMAF_AUDIO;
        break;
    }
}

void
AudioDriver_Paula::enableDMA()
{
    switch(outputMode) {
    case Mix:
        *((volatile mp_uword *) CUSTOM_DMACON) = DMAF_SETCLR | DMAF_AUD0 | DMAF_AUD1;
        break;
    case DirectOut:
        *((volatile mp_uword *) CUSTOM_DMACON) = DMAF_SETCLR | DMAF_AUDIO;
        break;
    }
}

void
AudioDriver_Paula::initHardware()
{
    int i;

    // Initialize audio hardware with default values
    mp_uword period = PAULA_CLK / this->mixFrequency;

    switch(outputMode) {
    case Mix:
        *((volatile mp_uint32 *) AUDIO_LOCHI(0)) = (mp_uint32) samplesLeft;
        *((volatile mp_uword *) AUDIO_LENGTH(0)) = chunkSize >> 1;
        *((volatile mp_uword *) AUDIO_PERIOD(0)) = period;

        *((volatile mp_uint32 *) AUDIO_LOCHI(1)) = (mp_uint32) samplesRight;
        *((volatile mp_uword *) AUDIO_LENGTH(1)) = chunkSize >> 1;
        *((volatile mp_uword *) AUDIO_PERIOD(1)) = period;

        break;
    case DirectOut:
        for(i = 0; i < MAX_CHANNELS; i++) {
            *((volatile mp_uint32 *) AUDIO_LOCHI(i)) = (mp_uint32) chanRing[i];
            *((volatile mp_uword *) AUDIO_LENGTH(i)) = chunkSize >> 1;
            *((volatile mp_uword *) AUDIO_PERIOD(i)) = period;
        }

        break;
    }
}

void
AudioDriver_Paula::playAudioImpl()
{
    int i;

    switch(outputMode) {
    case Mix:
        *((volatile mp_uint32 *) AUDIO_LOCHI(0)) = (mp_uint32) (samplesLeft + idxRead);
        *((volatile mp_uword *) AUDIO_LENGTH(0)) = chunkSize >> 1;

        *((volatile mp_uint32 *) AUDIO_LOCHI(1)) = (mp_uint32) (samplesRight + idxRead);
        *((volatile mp_uword *) AUDIO_LENGTH(1)) = chunkSize >> 1;

        break;
    case DirectOut:
        for(i = 0; i < MAX_CHANNELS; i++) {
            *((volatile mp_uint32 *) AUDIO_LOCHI(i)) = (mp_uint32) (chanRing[i] + idxRead);
            *((volatile mp_uword *) AUDIO_LENGTH(i)) = chunkSize >> 1;
        }

        break;
    }
}

void
AudioDriver_Paula::bufferAudioImpl()
{
    int i, j;

    switch(outputMode) {
    case Mix:
        {
            mp_sword * f = samplesFetched;
            mp_sbyte
                * l = samplesLeft + idxWrite,
                * r = samplesRight + idxWrite;

            if (isMixerActive())
                mixer->mixerHandler(f);
            else
                memset(f, 0, fetchSize << 2);

            for(i = 0; i < fetchSize; i++) {
                *(l++) = *(f++) >> 8;
                *(r++) = *(f++) >> 8;
            }
        }
        break;
    case DirectOut:
        {
            if (isMixerActive()) {
                for(i = 0; i < MAX_CHANNELS; i++)
                    mixerProxy->setBuffer<mp_sword>(i, chanFetch[i]);
                mixer->mixerHandler(NULL, mixerProxy);

                for(i = 0; i < MAX_CHANNELS; i++) {
                    mp_sword * s = chanFetch[i];
                    mp_sbyte * d = chanRing[i] + idxWrite;

                    for(j = 0; j < fetchSize; j++)
                        *(d++) = *(s++) >> 8;
                }
            } else {
                for(i = 0; i < MAX_CHANNELS; i++)
                    memset(chanRing[i] + idxWrite, 0, fetchSize * SAMPLE_SIZE);
            }
        }
        break;
    case ResampleHW:
        {
            if(isMixerActive()) {
                mixer->mixerHandler(NULL, mixerProxy);
            }
        }
        break;
    }
}


const char*
AudioDriver_Paula::getDriverID()
{
    return "Commodore Paula Audio 4-ch";
}

mp_sint32
AudioDriver_Paula::getPreferredBufferSize() const
{
    return 8192;
}

mp_sint32
AudioDriver_Paula::bufferAudio()
{
    if(outputMode == ResampleHW) {
        bufferAudioImpl();
    } else {
        return AudioDriver_Amiga<mp_sbyte>::bufferAudio();
    }
}

void
AudioDriver_Paula::disableIRQ()
{
    switch(outputMode) {
    case ResampleHW:
        break;
    default:
        AudioDriver_Amiga::disableIRQ();
        break;
    }
}

mp_sint32
AudioDriver_Paula::channelPlayback()
{
    // If Timer-B has been fired
    if(ciab.ciaicr & CIAICRF_TB) {
        if(hwDMACON) {
            //
            // First round
            //

            // Enable one-shot timer to set repeat
            ciab.ciacrb = CIACRBF_LOAD | CIACRBF_RUNMODE | CIACRBF_START;

            // Enable DMA channels
            custom.dmacon = DMAF_SETCLR | hwDMACON;
            hwDMACON = 0;
        } else {
            //
            // Second round
            //

            // Set sample pointers and lengths
            *((volatile mp_uint32 *) AUDIO_LOCHI(0)) = hwChannelLoopStart[0];
            *((volatile mp_uword *) AUDIO_LENGTH(0)) = hwChannelRepeatLength[0];
            *((volatile mp_uint32 *) AUDIO_LOCHI(1)) = hwChannelLoopStart[1];
            *((volatile mp_uword *) AUDIO_LENGTH(1)) = hwChannelRepeatLength[1];
            *((volatile mp_uint32 *) AUDIO_LOCHI(2)) = hwChannelLoopStart[2];
            *((volatile mp_uword *) AUDIO_LENGTH(2)) = hwChannelRepeatLength[2];
            *((volatile mp_uint32 *) AUDIO_LOCHI(3)) = hwChannelLoopStart[3];
            *((volatile mp_uword *) AUDIO_LENGTH(3)) = hwChannelRepeatLength[3];
        }
    }

    return 0;
}

void
AudioDriver_Paula::enableIRQ()
{
    switch(outputMode) {
    case ResampleHW:
        // Disable CIA interrupts
        ciab.ciaicr = CIAICRF_IR | CIAICRF_FLG | CIAICRF_SP | CIAICRF_ALRM | CIAICRF_TB | CIAICRF_TA;

        // Reset Timer A
        ciab.ciacrb = CIACRBF_LOAD;
        hwOldTimerLo = ciab.ciatblo;
        hwOldTimerHi = ciab.ciatbhi;
        ciab.ciatblo = 576&0xff;
        ciab.ciatbhi = 576>>8;

        // Enable CIA interrupt
        ciab.ciaicr = CIAICRF_SETCLR | CIAICRF_TB;

        // Enable channel playback interrupt
        AddIntServer(INTB_EXTER, irqChannelPlayback);
        custom.intena = INTF_SETCLR | INTF_EXTER;

        break;
    default:
        AudioDriver_Amiga::enableIRQ();
        break;
    }
}

void
AudioDriver_Paula::setChannelFrequency(ChannelMixer::TMixerChannel * chn)
{
    //printf("ch %ld per = %ld\n", chn->index, chn->period);

    *((volatile mp_uword *) AUDIO_PERIOD(chn->index)) = chn->period >> 10;
}

void
AudioDriver_Paula::setChannelVolume(ChannelMixer::TMixerChannel * chn)
{
    mp_sint32 vol = 0;

    switch (chn->index & 3) {
        case 0:
        case 3:
            vol = chn->finalvoll;
            break;
        case 1:
        case 2:
            vol = chn->finalvolr;
            break;
    }
    *((volatile mp_uword *) AUDIO_VOLUME(chn->index)) = ((vol >> 21) + 6) >> 3;
}

void
AudioDriver_Paula::playSample(ChannelMixer::TMixerChannel * chn)
{
    printf("ch %ld play = $%08lx smppos = $%08lx loopend = $%08lx\n", chn->index, chn->sample, chn->smppos, chn->loopend);

    ciab.ciacrb = CIACRBF_LOAD;

    // Stop running sample
    *((volatile mp_uword *) CUSTOM_DMACON) = DMAF_AUD0 << chn->index;

    // Set sample
    *((volatile mp_uint32 *) AUDIO_LOCHI(chn->index)) = (mp_uint32) (chn->sample + chn->smppos);
    *((volatile mp_uword *) AUDIO_LENGTH(chn->index)) = (mp_uword) (((chn->loopend - chn->smppos) >> 1) & 0xffff);
    setChannelFrequency(chn);
    setChannelVolume(chn);

    if ((chn->flags & 3) == 0) {
        hwChannelLoopStart[chn->index] = (mp_uint32) (chn->sample + chn->loopstart);
        hwChannelRepeatLength[chn->index] = 1;
    } else {
        hwChannelLoopStart[chn->index] = (mp_uint32) (chn->sample + chn->loopstart);
        hwChannelRepeatLength[chn->index] = (mp_uword) (((chn->loopend - chn->loopstart) >> 1) & 0xffff);
    }

    // And mark DMA channel as to be played
    hwDMACON |= DMAF_AUD0 << chn->index;

    //
    // After processing plays and stops, this continues in tickDone
    // to enable the interrupt which enables the DMA
    //
}

void
AudioDriver_Paula::stopSample(ChannelMixer::TMixerChannel * chn)
{
    printf("ch %ld stop = %ld\n", chn->index, chn->sample);

    // Stop running sample
    *((volatile mp_uword *) CUSTOM_DMACON) = DMAF_AUD0 << chn->index;

    //
    // See playSample what happens next!
    //
}

void
AudioDriver_Paula::tickDone()
{
    // If there are new samples playing on a channel, let irqEnableChannels enable the DMA for us
    if(hwDMACON) {
        // Enable one-shot timer to set repeat
        ciab.ciacrb = CIACRBF_LOAD | CIACRBF_RUNMODE | CIACRBF_START;
    }
}
