#include <proto/exec.h>
#include <hardware/custom.h>
#include <hardware/dmabits.h>
#include <hardware/intbits.h>
#include <hardware/cia.h>

#include "AudioDriver_SAGA.h"
#include "MasterMixer.h"

extern volatile struct Custom   custom;

#define CIAAPRA                 0xbfe001

#define SAGA_REGBASE            0xdff000

#define SAGA_REGBANK(N)         (SAGA_REGBASE + ((N) << 9))

#define SAGA_DMACON(BNK)        (SAGA_REGBANK(BNK) + 0x96)

#define SAGA_AUDIO_INTENA(CH)   (SAGA_REGBANK((CH) >> 2) + 0x9a)
#define SAGA_AUDIO_REGBASE(CH)  (SAGA_REGBANK((CH) >> 2) + 0xa0 + (((CH) & 0x3) << 4))
#define SAGA_AUDIO_REG(CH, IDX) (SAGA_AUDIO_REGBASE(CH) + (IDX))

#define SAGA_AUDIO_LOCHI(CH)    SAGA_AUDIO_REG(CH, 0x00)
#define SAGA_AUDIO_LOCLO(CH)    SAGA_AUDIO_REG(CH, 0x02)
#define SAGA_AUDIO_LENLO(CH)    SAGA_AUDIO_REG(CH, 0x04)
#define SAGA_AUDIO_PERIOD(CH)   SAGA_AUDIO_REG(CH, 0x06)
#define SAGA_AUDIO_VOLUME(CH)   SAGA_AUDIO_REG(CH, 0x08)
#define SAGA_AUDIO_DATA(CH)     SAGA_AUDIO_REG(CH, 0x0a)
#define SAGA_AUDIO_VOLFAR(CH)   SAGA_AUDIO_REG(CH, 0x0c)
#define SAGA_AUDIO_LENHI(CH)    SAGA_AUDIO_REG(CH, 0x0e)

#define DEBUG_DRIVER    1
#define MAX_BANKS       2
#define MAX_CHANNELS    (MAX_BANKS << 2)
#define MAX_VOLUME      0x40
#define PI_F            3.14159265358979323846f

#define SAMPLES_CHUNK   512
#define SAMPLES_RING    16384
#define SAMPLES_FETCH   2048 // @todo fix make depedendant from requested format

AudioDriver_SAGA::AudioDriver_SAGA()
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
        // Bounced buffer for 16-bit stereo (4 byte a sample)
        samplesBounced = AllocMem(SAMPLES_FETCH << 2, MEMF_PUBLIC | MEMF_CLEAR);

        // Converted buffers for 8-bit mono (1 byte a sample)
        samplesLeft = AllocMem(SAMPLES_RING, MEMF_CHIP | MEMF_CLEAR);
        samplesRight = AllocMem(SAMPLES_RING, MEMF_CHIP | MEMF_CLEAR);
    } else {
        // Ring buffers for each channel
        chanRing = AllocMem(MAX_CHANNELS * sizeof(mp_sbyte *), MEMF_PUBLIC | MEMF_CLEAR);
        for(i = 0; i < MAX_CHANNELS; i++)
            chanRing[i] = AllocMem(SAMPLES_RING, MEMF_CHIP | MEMF_CLEAR);
        chanRingPtrs = AllocMem(MAX_CHANNELS * sizeof(mp_sbyte *), MEMF_PUBLIC | MEMF_CLEAR);

        // Panning for each channel
        chanPan = AllocMem(MAX_CHANNELS * sizeof(mp_sword), MEMF_PUBLIC | MEMF_CLEAR);
    }
}

AudioDriver_SAGA::~AudioDriver_SAGA()
{
    int i;

    if(!directOut) {
        FreeMem(samplesRight, SAMPLES_RING);
        FreeMem(samplesLeft, SAMPLES_RING);

        FreeMem(samplesBounced, SAMPLES_FETCH << 2);

        FreeMem(irqBufferAudio, sizeof(struct Interrupt));
        FreeMem(irqPlayAudio, sizeof(struct Interrupt));
    } else {
        FreeMem(chanPan, MAX_CHANNELS * sizeof(mp_sword));

        for(i = 0; i < MAX_CHANNELS; i++)
            FreeMem(chanRing[i], SAMPLES_RING);
        FreeMem(chanRing, MAX_CHANNELS * sizeof(mp_sbyte *));
        FreeMem(chanRingPtrs, MAX_CHANNELS * sizeof(mp_sbyte *));
    }
}

void
AudioDriver_SAGA::setGlobalVolume(mp_ubyte volume)
{
    int i;

    for(i = 0; i < MAX_CHANNELS; i++) {
        *((volatile mp_uword *) SAGA_AUDIO_VOLUME(i)) = volume;
        //*((volatile mp_uword *) SAGA_AUDIO_VOLFAR(i)) = 0;
    }
}

void
AudioDriver_SAGA::disableDMA()
{
    int i;

    if(!directOut) {
        *((volatile mp_uword *) SAGA_DMACON(0)) = DMAF_AUD0 | DMAF_AUD1;
    } else {
        for(i = 0; i < MAX_BANKS; i++) {
            *((volatile mp_uword *) SAGA_DMACON(i)) = DMAF_AUDIO;
        }
    }
}

void
AudioDriver_SAGA::enableDMA()
{
    int i;

    if(!directOut) {
        *((volatile mp_uword *) SAGA_DMACON(0)) = DMAF_SETCLR | DMAF_AUD0 | DMAF_AUD1;
    } else {
        for(i = 0; i < MAX_BANKS; i++) {
            *((volatile mp_uword *) SAGA_DMACON(i)) = DMAF_SETCLR | DMAF_AUDIO;
        }
    }
}

void
AudioDriver_SAGA::disableIRQ()
{
    custom.intena = INTF_AUD0;

    if(irqAudioOld != NULL) {
        RemIntServer(INTB_VERTB, irqBufferAudio);
        SetIntVector(INTB_AUD0, irqAudioOld);
        irqAudioOld = NULL;
    }
}

void
AudioDriver_SAGA::enableIRQ()
{
    if(irqAudioOld == NULL) {
        irqAudioOld = SetIntVector(INTB_AUD0, irqPlayAudio);
        AddIntServer(INTB_VERTB, irqBufferAudio);
    }

    custom.intena = INTF_SETCLR | INTF_AUD0;
}

void
AudioDriver_SAGA::playAudioService(register AudioDriver_SAGA * that __asm("a1"))
{
    that->playAudio();
}

void
AudioDriver_SAGA::playAudio()
{
    int i;

    if(!directOut) {
        *((volatile mp_uint32 *) SAGA_AUDIO_LOCHI(0)) = (mp_uint32) samplesLeft + idxRead;
        *((volatile mp_uword *) SAGA_AUDIO_LENLO(0)) = SAMPLES_CHUNK >> 1;

        *((volatile mp_uint32 *) SAGA_AUDIO_LOCHI(1)) = (mp_uint32) samplesRight + idxRead;
        *((volatile mp_uword *) SAGA_AUDIO_LENLO(1)) = SAMPLES_CHUNK >> 1;
    } else {
        for(i = 0; i < MAX_CHANNELS; i++) {
            *((volatile mp_uint32 *) SAGA_AUDIO_LOCHI(i)) = (mp_uint32) chanRing[i] + idxRead;
            *((volatile mp_uword *) SAGA_AUDIO_LENLO(i)) = SAMPLES_CHUNK >> 1;
        }
    }

    idxRead += SAMPLES_CHUNK;
    if(idxRead >= SAMPLES_RING)
        idxRead = 0;

    custom.intreq = INTF_AUD0;
}

mp_sint32
AudioDriver_SAGA::bufferAudioService(register AudioDriver_SAGA * that __asm("a1"))
{
    return that->bufferAudio();
}

mp_sint32
AudioDriver_SAGA::bufferAudio()
{
    int i;

    // Fetch only if we would not write into the block to read
    mp_sint32 idxDist;
    if(idxRead <= idxWrite)
        idxDist = idxWrite - idxRead;
    else
        idxDist = (SAMPLES_RING - idxRead) + idxWrite;

    if(idxDist >= SAMPLES_FETCH) {
		MasterMixer* mixer = this->mixer;

        if(!directOut) {
            mp_sword * b = samplesBounced;
            mp_sbyte
                * l = samplesLeft + idxWrite,
                * r = samplesRight + idxWrite;

            if (isMixerActive())
                mixer->mixerHandler(b);
            else
                memset(b, 0, SAMPLES_FETCH << 2);

            // Deinterlave and scale down to 8-bit
            for(i = 0; i < SAMPLES_FETCH; i++) {
                *(l++) = *(b++) >> 8;
                *(r++) = *(b++) >> 8;
            }
        } else {
            if (isMixerActive()) {
                for(i = 0; i < MAX_CHANNELS; i++) {
                    chanRingPtrs[i] = chanRing[i] + idxWrite;
                }
                mixer->mixerHandler(NULL, MAX_CHANNELS, chanRingPtrs);
            } else {
                for(i = 0; i < MAX_CHANNELS; i++) {
                    memset(chanRing[i] + idxWrite, 0, SAMPLES_FETCH);
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
AudioDriver_SAGA::initDevice(mp_sint32 bufferSizeInWords, mp_uint32 mixFrequency, MasterMixer* mixer)
{
    int i;

#if DEBUG_DRIVER
    printf("AudioDriver_SAGA::initDevice(%ld, %ld, %lx)\n", bufferSizeInWords, mixFrequency, mixer);
    printf("INTENAR: %lx DMACONR: %lx\n", custom.intenar, custom.dmaconr);
#endif

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
    if(!directOut) {
        *((volatile mp_uint32 *) SAGA_AUDIO_LOCHI(0)) = (mp_uint32) samplesLeft;
        *((volatile mp_uword *) SAGA_AUDIO_LENLO(0)) = SAMPLES_CHUNK >> 1;
        *((volatile mp_uword *) SAGA_AUDIO_PERIOD(0)) = 161; // 22030Hz

        *((volatile mp_uint32 *) SAGA_AUDIO_LOCHI(1)) = (mp_uint32) samplesRight;
        *((volatile mp_uword *) SAGA_AUDIO_LENLO(1)) = SAMPLES_CHUNK >> 1;
        *((volatile mp_uword *) SAGA_AUDIO_PERIOD(1)) = 161; // 22030Hz
    } else {
        for(i = 0; i < MAX_CHANNELS; i++) {
            *((volatile mp_uint32 *) SAGA_AUDIO_LOCHI(i)) = (mp_uint32) chanRing[i];
            *((volatile mp_uword *) SAGA_AUDIO_LENLO(i)) = SAMPLES_CHUNK >> 1;
            *((volatile mp_uword *) SAGA_AUDIO_PERIOD(i)) = 161; // 22030Hz
        }
    }

    /** @see MasterMixer::openAudioDevice() to understand the implications */
	return 0;
}

mp_sint32
AudioDriver_SAGA::closeDevice()
{
#if DEBUG_DRIVER
    printf("AudioDriver_SAGA::closeDevice()\n");
#endif
    setGlobalVolume(0);
    disableDMA();

    custom.intenar = intenaOld;
    custom.dmaconr = dmaconOld;

	return MP_OK;
}

mp_sint32
AudioDriver_SAGA::start()
{
#if DEBUG_DRIVER
    printf("AudioDriver_SAGA::start()\n");
#endif
    setGlobalVolume(MAX_VOLUME);
    enableDMA();
    enableIRQ();

	return MP_OK;
}

mp_sint32
AudioDriver_SAGA::stop()
{
#if DEBUG_DRIVER
    printf("AudioDriver_SAGA::stop()\n");
#endif
    setGlobalVolume(0);
    disableDMA();

    return MP_OK;
}

mp_sint32
AudioDriver_SAGA::pause()
{
#if DEBUG_DRIVER
    printf("AudioDriver_SAGA::pause()\n");
#endif
    disableDMA();

    return MP_OK;
}

mp_sint32
AudioDriver_SAGA::resume()
{
#if DEBUG_DRIVER
    printf("AudioDriver_SAGA::resume()\n");
#endif
    enableDMA();

    return MP_OK;
}

const char*
AudioDriver_SAGA::getDriverID()
{
    return "SAGAAudio";
}

mp_sint32
AudioDriver_SAGA::getPreferredBufferSize() const
{
    return directOut ? SAMPLES_FETCH : SAMPLES_FETCH << 2;
}
