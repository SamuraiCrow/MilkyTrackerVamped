#include <proto/exec.h>
#include <hardware/custom.h>
#include <hardware/dmabits.h>
#include <hardware/intbits.h>
#include <hardware/cia.h>

#include "AudioDriver_Pamela.h"
#include "MasterMixer.h"

extern volatile struct Custom   custom;

#define CIAAPRA                 0xbfe001

#define CUSTOM_REGBASE          0xdff000

#define CUSTOM_DMACON           (CUSTOM_REGBASE + 0x096)
#define CUSTOM_DMACON2          (CUSTOM_DMACON  + 0x200)
#define CUSTOM_INTENA           (CUSTOM_REGBASE + 0x09a)
#define CUSTOM_INTENA2          (CUSTOM_INTENA  + 0x200)

#ifdef PAULA

#   define AUDIO_REGBASE(CH)        (CUSTOM_REGBASE((CH) >> 2) + 0xa0 + (((CH) & 0x3) << 4))
#   define AUDIO_REG(CH, IDX)       (AUDIO_REGBASE(CH) + (IDX))

#   define AUDIO_LOCHI(CH)          AUDIO_REG(CH, 0x00)
#   define AUDIO_LOCLO(CH)          AUDIO_REG(CH, 0x02)
#   define AUDIO_LENLO(CH)          AUDIO_REG(CH, 0x04)
#   define AUDIO_PERIOD(CH)         AUDIO_REG(CH, 0x06)
#   define AUDIO_VOLUME(CH)         AUDIO_REG(CH, 0x08)

#   define AUDIO_LEN_SHIFT          1

#else

#   define AUDIO_REGBASE(CH)        (CUSTOM_REGBASE + 0x400 + ((CH) << 4))
#   define AUDIO_REG(CH, IDX)       (AUDIO_REGBASE(CH) + (IDX))

#   define AUDIO_LOCHI(CH)          AUDIO_REG(CH, 0x00)
#   define AUDIO_LOCLO(CH)          AUDIO_REG(CH, 0x02)
#   define AUDIO_LENHI(CH)          AUDIO_REG(CH, 0x04)
#   define AUDIO_LENLO(CH)          AUDIO_REG(CH, 0x06)
#   define AUDIO_VOLUME(CH)         AUDIO_REG(CH, 0x08)
#   define AUDIO_MODE(CH)           AUDIO_REG(CH, 0x0a)
#   define AUDIO_PERIOD(CH)         AUDIO_REG(CH, 0x0c)

#   define AUDIO_MODEF_16           (1<<0)
#   define AUDIO_MODEF_ONESHOT      (1<<1)

#   define DMA2F_AUDIO              0xfff

#   define AUDIO_LEN_SHIFT          1

#endif

#define DEBUG_DRIVER            1
#define MAX_VOLUME              0x40

#define PI_F                    3.14159265358979323846f
#define PAULA_CLK_PAL           3546895

#define SAMPLES_CHUNK           512
#define SAMPLES_RING            16384
#define SAMPLES_FETCH           2048 // @todo fix make depedendant from requested format

AudioDriver_Pamela::AudioDriver_Pamela()
: AudioDriverBase()
, irqAudioOld(NULL)
, idxRead(0)
, idxWrite(SAMPLES_RING >> 1)
, directOut(true)
{
    int i;

    irqPlayAudio = AllocMem(sizeof(struct Interrupt), MEMF_PUBLIC | MEMF_CLEAR);
    irqBufferAudio = AllocMem(sizeof(struct Interrupt), MEMF_PUBLIC | MEMF_CLEAR);

    if(!directOut) {
        // Fetch buffer for 16-bit stereo (4 byte a sample)
        samplesFetched = AllocMem(SAMPLES_FETCH << 2, MEMF_PUBLIC | MEMF_CLEAR);

        // Ring buffers for each side
        samplesLeft = AllocMem(SAMPLES_RING * SAMPLE_SIZE, MEMF_CHIP | MEMF_CLEAR);
        samplesRight = AllocMem(SAMPLES_RING * SAMPLE_SIZE, MEMF_CHIP | MEMF_CLEAR);
    } else {
        // Ring buffers for each channel
        chanFetch = AllocMem(MAX_CHANNELS * sizeof(mp_sword *), MEMF_PUBLIC | MEMF_CLEAR);
        chanRing = AllocMem(MAX_CHANNELS * sizeof(mp_smptype *), MEMF_PUBLIC | MEMF_CLEAR);

        for(i = 0; i < MAX_CHANNELS; i++) {
            chanFetch[i] = AllocMem(SAMPLES_FETCH * sizeof(mp_sword), MEMF_PUBLIC | MEMF_CLEAR);
            chanRing[i] = AllocMem(SAMPLES_RING * SAMPLE_SIZE, MEMF_CHIP | MEMF_CLEAR);
        }

        chanRingPtrs = AllocMem(MAX_CHANNELS * sizeof(mp_smptype *), MEMF_PUBLIC | MEMF_CLEAR);
    }
}

AudioDriver_Pamela::~AudioDriver_Pamela()
{
    int i;

    if(!directOut) {
        FreeMem(samplesRight, SAMPLES_RING * SAMPLE_SIZE);
        FreeMem(samplesLeft, SAMPLES_RING * SAMPLE_SIZE);

        FreeMem(samplesFetched, SAMPLES_FETCH << 2);
    } else {
        FreeMem(chanRingPtrs, MAX_CHANNELS * sizeof(mp_smptype *));

        for(i = 0; i < MAX_CHANNELS; i++) {
            FreeMem(chanRing[i], SAMPLES_RING * SAMPLE_SIZE);
            FreeMem(chanFetch[i], SAMPLES_FETCH * sizeof(mp_sword));
        }

        FreeMem(chanRing, MAX_CHANNELS * sizeof(mp_smptype *));
        FreeMem(chanFetch, MAX_CHANNELS * sizeof(mp_sword *));
    }

    FreeMem(irqBufferAudio, sizeof(struct Interrupt));
    FreeMem(irqPlayAudio, sizeof(struct Interrupt));
}

void
AudioDriver_Pamela::setGlobalVolume(mp_ubyte volume)
{
    int i;

    for(i = 0; i < MAX_CHANNELS; i++) {
        *((volatile mp_uword *) AUDIO_VOLUME(i)) = volume;
    }
}

void
AudioDriver_Pamela::disableDMA()
{
    int i;

    if(!directOut) {
        *((volatile mp_uword *) CUSTOM_DMACON) = DMAF_AUD0 | DMAF_AUD1;
    } else {
        *((volatile mp_uword *) CUSTOM_DMACON) = DMAF_AUDIO;
#ifndef PAULA
        *((volatile mp_uword *) CUSTOM_DMACON2) = DMA2F_AUDIO;
#endif
    }
}

void
AudioDriver_Pamela::enableDMA()
{
    int i;

    if(!directOut) {
        *((volatile mp_uword *) CUSTOM_DMACON) = DMAF_SETCLR | DMAF_AUD0 | DMAF_AUD1;
    } else {
        *((volatile mp_uword *) CUSTOM_DMACON) = DMAF_SETCLR | DMAF_AUDIO;
#ifndef PAULA
        *((volatile mp_uword *) CUSTOM_DMACON2) = DMAF_SETCLR | DMA2F_AUDIO;
#endif
    }
}

void
AudioDriver_Pamela::disableIRQ()
{
    custom.intena = INTF_AUD0;

    if(irqAudioOld != NULL) {
        RemIntServer(INTB_VERTB, irqBufferAudio);
        SetIntVector(INTB_AUD0, irqAudioOld);
        irqAudioOld = NULL;
    }
}

void
AudioDriver_Pamela::enableIRQ()
{
    if(irqAudioOld == NULL) {
        irqAudioOld = SetIntVector(INTB_AUD0, irqPlayAudio);
        AddIntServer(INTB_VERTB, irqBufferAudio);
    }

    custom.intena = INTF_SETCLR | INTF_AUD0;
}

void
AudioDriver_Pamela::playAudioService(register AudioDriver_Pamela * that __asm("a1"))
{
    that->playAudio();
}

void
AudioDriver_Pamela::playAudio()
{
    int i;

    if(!directOut) {
        *((volatile mp_uint32 *) AUDIO_LOCHI(0)) = (mp_uint32) (samplesLeft + idxRead);
        *((volatile mp_uword *) AUDIO_LENLO(0)) = SAMPLES_CHUNK >> AUDIO_LEN_SHIFT;

        *((volatile mp_uint32 *) AUDIO_LOCHI(1)) = (mp_uint32) (samplesRight + idxRead);
        *((volatile mp_uword *) AUDIO_LENLO(1)) = SAMPLES_CHUNK >> AUDIO_LEN_SHIFT;
    } else {
        for(i = 0; i < MAX_CHANNELS; i++) {
            *((volatile mp_uint32 *) AUDIO_LOCHI(i)) = (mp_uint32) (chanRing[i] + idxRead);
            *((volatile mp_uword *) AUDIO_LENLO(i)) = SAMPLES_CHUNK >> AUDIO_LEN_SHIFT;
        }
    }

    idxRead += SAMPLES_CHUNK;
    if(idxRead >= SAMPLES_RING)
        idxRead = 0;

    custom.intreq = INTF_AUD0;
}

mp_sint32
AudioDriver_Pamela::bufferAudioService(register AudioDriver_Pamela * that __asm("a1"))
{
    return that->bufferAudio();
}

mp_sint32
AudioDriver_Pamela::bufferAudio()
{
    int i, j;

    // Fetch only if we would not write into the block to read
    mp_sint32 idxDist;
    if(idxRead <= idxWrite)
        idxDist = idxWrite - idxRead;
    else
        idxDist = (SAMPLES_RING - idxRead) + idxWrite;

    if(idxDist >= SAMPLES_FETCH) {
		MasterMixer* mixer = this->mixer;

        if(!directOut) {
            mp_sword * f = samplesFetched;
            mp_smptype
                * l = samplesLeft + idxWrite,
                * r = samplesRight + idxWrite;

            if (isMixerActive())
                mixer->mixerHandler(f);
            else
                memset(f, 0, SAMPLES_FETCH << 2);

            // Deinterleave stereo
            for(i = 0; i < SAMPLES_FETCH; i++) {
#ifdef PAULA
                *(l++) = *(f++) >> 8;
                *(r++) = *(f++) >> 8;
#else
                *(l++) = *(f++);
                *(r++) = *(f++);
#endif
            }
        } else {
            if (isMixerActive()) {
#ifdef PAULA
                mixer->mixerHandler(NULL, MAX_CHANNELS, chanFetch);

                for(i = 0; i < MAX_CHANNELS; i++) {
                    mp_sword * s = chanFetch[i];
                    mp_smptype * d = chanRing[i] + idxWrite;

                    for(j = 0; j < SAMPLES_FETCH; j++) {
                        *(d++) = *(s++) >> 8;
                    }
                }
#else
                for(i = 0; i < MAX_CHANNELS; i++) {
                    chanRingPtrs[i] = chanRing[i] + idxWrite;
                }
                mixer->mixerHandler(NULL, MAX_CHANNELS, chanRingPtrs);
#endif
            } else {
                for(i = 0; i < MAX_CHANNELS; i++) {
                    memset(chanRing[i] + idxWrite, 0, SAMPLES_FETCH * SAMPLE_SIZE);
                }
            }
        }

        idxWrite += SAMPLES_FETCH;
        if(idxWrite >= SAMPLES_RING)
            idxWrite = 0;
    }

    return 1;
}


mp_sint32
AudioDriver_Pamela::initDevice(mp_sint32 bufferSizeInWords, mp_uint32 mixFrequency, MasterMixer* mixer)
{
    int i;

#if DEBUG_DRIVER
    printf("AudioDriver_Pamela::initDevice(%ld, %ld, %lx)\n", bufferSizeInWords, mixFrequency, mixer);
    printf("INTENAR: %lx DMACONR: %lx\n", custom.intenar, custom.dmaconr);
#endif

#ifdef PAULA
    mixFrequency = 22050;
#else
    mixFrequency = 44100;
#endif
    printf("Forcing mix frequency to %ld hz\n", mixFrequency);

	mp_sint32 res = AudioDriverBase::initDevice(bufferSizeInWords, mixFrequency, mixer);
	if (res < 0) {
		return res;
	}

    //            _IEDRAAAABVCPSDT
    // INTENA 60c2 110000011000010
    //            _BB__BDBCBSDAAAA
    // DMACON 23f0 010001111110000

    intenaOld = custom.intenar;
    dmaconOld = custom.dmaconr;

    // Create interrupt for buffering
    irqBufferAudio->is_Node.ln_Type = NT_INTERRUPT;
    irqBufferAudio->is_Node.ln_Name = (char *) "mt-saga-buf-irq";
    irqBufferAudio->is_Data = this;
    irqBufferAudio->is_Code = (void(*)()) bufferAudioService;

    // Create interrupt for playback
    irqPlayAudio->is_Node.ln_Type = NT_INTERRUPT;
    irqPlayAudio->is_Node.ln_Name = (char *) "mt-saga-play-irq";
    irqPlayAudio->is_Data = this;
    irqPlayAudio->is_Code = (void(*)()) playAudioService;

    // Zero volume & disable audio DMA
    setGlobalVolume(0);
    disableDMA();
    disableIRQ();

    // Disable lowpass filter and reset ADKCON
    *((volatile mp_ubyte *) CIAAPRA) |= CIAF_LED;
    custom.adkcon = 0xff;

    // Initialize audio hardware with default values
    mp_uword period = PAULA_CLK_PAL / this->mixFrequency;

    if(!directOut) {
        *((volatile mp_uint32 *) AUDIO_LOCHI(0)) = (mp_uint32) samplesLeft;
        *((volatile mp_uword *) AUDIO_LENLO(0)) = SAMPLES_CHUNK >> AUDIO_LEN_SHIFT;
        *((volatile mp_uword *) AUDIO_PERIOD(0)) = period;

        *((volatile mp_uint32 *) AUDIO_LOCHI(1)) = (mp_uint32) samplesRight;
        *((volatile mp_uword *) AUDIO_LENLO(1)) = SAMPLES_CHUNK >> AUDIO_LEN_SHIFT;
        *((volatile mp_uword *) AUDIO_PERIOD(1)) = period;
    } else {
        for(i = 0; i < MAX_CHANNELS; i++) {
            *((volatile mp_uint32 *) AUDIO_LOCHI(i)) = (mp_uint32) chanRing[i];
            *((volatile mp_uword *) AUDIO_LENLO(i)) = SAMPLES_CHUNK >> AUDIO_LEN_SHIFT;
            *((volatile mp_uword *) AUDIO_PERIOD(i)) = period;

#ifndef PAULA
            *((volatile mp_uword *) AUDIO_MODE(i)) = AUDIO_MODEF_16;
#endif
        }
    }

    /** @see MasterMixer::openAudioDevice() to understand the implications */
	return 0;
}

mp_sint32
AudioDriver_Pamela::closeDevice()
{
#if DEBUG_DRIVER
    printf("AudioDriver_Pamela::closeDevice()\n");
#endif
    setGlobalVolume(0);
    disableDMA();

    custom.intenar = intenaOld;
    custom.dmaconr = dmaconOld;

	return MP_OK;
}

mp_sint32
AudioDriver_Pamela::start()
{
#if DEBUG_DRIVER
    printf("AudioDriver_Pamela::start()\n");
#endif
    setGlobalVolume(MAX_VOLUME);
    enableDMA();
    enableIRQ();

	return MP_OK;
}

mp_sint32
AudioDriver_Pamela::stop()
{
#if DEBUG_DRIVER
    printf("AudioDriver_Pamela::stop()\n");
#endif
    setGlobalVolume(0);
    disableDMA();
    disableIRQ();

    return MP_OK;
}

mp_sint32
AudioDriver_Pamela::pause()
{
#if DEBUG_DRIVER
    printf("AudioDriver_Pamela::pause()\n");
#endif
    disableDMA();

    return MP_OK;
}

mp_sint32
AudioDriver_Pamela::resume()
{
#if DEBUG_DRIVER
    printf("AudioDriver_Pamela::resume()\n");
#endif
    enableDMA();

    return MP_OK;
}

const char*
AudioDriver_Pamela::getDriverID()
{
    return "PamelaAudio";
}

mp_sint32
AudioDriver_Pamela::getPreferredBufferSize() const
{
    // @fuckthat This is number of samples!!!!!!!!!! Do return sample buffer size here. Not BYTE buffer size!!!!!!!!!!!!!!!
    return SAMPLES_FETCH;
}
