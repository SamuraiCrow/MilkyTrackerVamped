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

#if defined(AMIGA_PAULA)

#   define AUDIO_REGBASE(CH)        (CUSTOM_REGBASE + 0xa0 + (((CH) & 0x3) << 4))
#   define AUDIO_REG(CH, IDX)       (AUDIO_REGBASE(CH) + (IDX))

#   define AUDIO_LOCHI(CH)          AUDIO_REG(CH, 0x00)
#   define AUDIO_LOCLO(CH)          AUDIO_REG(CH, 0x02)
#   define AUDIO_LENLO(CH)          AUDIO_REG(CH, 0x04)
#   define AUDIO_PERIOD(CH)         AUDIO_REG(CH, 0x06)
#   define AUDIO_VOLUME(CH)         AUDIO_REG(CH, 0x08)

#   define AUDIO_LEN_SHIFT          1

#else

#   define AUDIO_REGBASE(CH)        (CUSTOM_REGBASE + 0x400 + ((CH & 0xf) << 4))
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

#define PAULA_CLK_PAL           3546895
#define PAL_LINES               312

#define SAMPLE_SIZE             MP_NUMBYTES

AudioDriver_Pamela::AudioDriver_Pamela()
: AudioDriverBase()
, irqAudioOld(NULL)
, allocated(false)
, statVerticalBlankMixMedian(0)
, statAudioBufferReset(0)
, statAudioBufferResetMedian(0)
, statRingBufferFull(0)
, statRingBufferFullMedian(0)
, statCountPerSecond(0)
{
    irqPlayAudio = AllocMem(sizeof(struct Interrupt), MEMF_PUBLIC | MEMF_CLEAR);
    irqBufferAudio = AllocMem(sizeof(struct Interrupt), MEMF_PUBLIC | MEMF_CLEAR);
}

AudioDriver_Pamela::~AudioDriver_Pamela()
{
    dealloc();

    FreeMem(irqBufferAudio, sizeof(struct Interrupt));
    FreeMem(irqPlayAudio, sizeof(struct Interrupt));
}

mp_sint32
AudioDriver_Pamela::getStatValue(mp_uint32 key)
{
    switch(key) {
    case 0:
        return statVerticalBlankMixMedian;
    case 1:
        return statAudioBufferResetMedian;
    case 2:
        return statRingBufferFullMedian;
    }

    return 0;
}

mp_sint32
AudioDriver_Pamela::alloc(mp_sint32 bufferSize)
{
    int i;

    dealloc();

    // Force buffer size of 2^n
    for (mp_uint32 i = 0; i < 16; i++) {
        if ((unsigned)(1 << i) >= (unsigned)bufferSize) {
            bufferSize = 1 << i;
            break;
        }
    }

    // Number of samples to fetch from Milky's ChannelMixer
    // ! Needs to be the same as the requested size
    fetchSize = bufferSize;

    // Number of samples in ring buffer we write into *and* read from at the same time
    ringSize = fetchSize << 2;

    // Number of samples processed via DMA for each audio block
    chunkSize = fetchSize >> 2;

    // Reset ring buffer indices
    idxRead = 0;
    idxWrite = ringSize >> 1;

    // Reset stat values
    statVerticalBlankMixMedian = 0;
    statAudioBufferReset = 0;
    statAudioBufferResetMedian = 0;
    statRingBufferFull = 0;
    statRingBufferFullMedian = 0;
    statCountPerSecond = 0;

#if defined(AMIGA_DIRECTOUT)
    // Ring buffers for each channel
    chanFetch = AllocMem(MAX_CHANNELS * sizeof(mp_sword *), MEMF_PUBLIC | MEMF_CLEAR);
    chanRing = AllocMem(MAX_CHANNELS * sizeof(mp_smptype *), MEMF_PUBLIC | MEMF_CLEAR);

    for(i = 0; i < MAX_CHANNELS; i++) {
        chanFetch[i] = AllocMem(fetchSize * sizeof(mp_sword), MEMF_PUBLIC | MEMF_CLEAR);
        chanRing[i] = AllocMem(ringSize * SAMPLE_SIZE, MEMF_CHIP | MEMF_CLEAR);
    }

    chanRingPtrs = AllocMem(MAX_CHANNELS * sizeof(mp_smptype *), MEMF_PUBLIC | MEMF_CLEAR);
#else
    // Fetch buffer for 16-bit stereo (4 byte a sample)
    samplesFetched = AllocMem(fetchSize << 2, MEMF_PUBLIC | MEMF_CLEAR);

    // Ring buffers for each side
    samplesLeft = AllocMem(ringSize * SAMPLE_SIZE, MEMF_CHIP | MEMF_CLEAR);
    samplesRight = AllocMem(ringSize * SAMPLE_SIZE, MEMF_CHIP | MEMF_CLEAR);
#endif

    // Initialize audio hardware with default values
    mp_uword period = PAULA_CLK_PAL / this->mixFrequency;

#if defined(AMIGA_DIRECTOUT)
    for(i = 0; i < MAX_CHANNELS; i++) {
        *((volatile mp_uint32 *) AUDIO_LOCHI(i)) = (mp_uint32) chanRing[i];
        *((volatile mp_uword *) AUDIO_LENLO(i)) = chunkSize >> AUDIO_LEN_SHIFT;
        *((volatile mp_uword *) AUDIO_PERIOD(i)) = period;
#   if !defined(AMIGA_PAULA)
        *((volatile mp_uword *) AUDIO_MODE(i)) = AUDIO_MODEF_16;
#   endif
    }
#else
    *((volatile mp_uint32 *) AUDIO_LOCHI(0)) = (mp_uint32) samplesLeft;
    *((volatile mp_uword *) AUDIO_LENLO(0)) = chunkSize >> AUDIO_LEN_SHIFT;
    *((volatile mp_uword *) AUDIO_PERIOD(0)) = period;

    *((volatile mp_uint32 *) AUDIO_LOCHI(1)) = (mp_uint32) samplesRight;
    *((volatile mp_uword *) AUDIO_LENLO(1)) = chunkSize >> AUDIO_LEN_SHIFT;
    *((volatile mp_uword *) AUDIO_PERIOD(1)) = period;
#endif

    allocated = true;

    return bufferSize;
}

void
AudioDriver_Pamela::dealloc()
{
    int i;

    if(!allocated)
        return;
    allocated = false;

#if defined(AMIGA_DIRECTOUT)
    FreeMem(chanRingPtrs, MAX_CHANNELS * sizeof(mp_smptype *));

    for(i = 0; i < MAX_CHANNELS; i++) {
        FreeMem(chanRing[i], ringSize * SAMPLE_SIZE);
        FreeMem(chanFetch[i], fetchSize * sizeof(mp_sword));
    }

    FreeMem(chanRing, MAX_CHANNELS * sizeof(mp_smptype *));
    FreeMem(chanFetch, MAX_CHANNELS * sizeof(mp_sword *));
#else
    FreeMem(samplesRight, ringSize * SAMPLE_SIZE);
    FreeMem(samplesLeft, ringSize * SAMPLE_SIZE);

    FreeMem(samplesFetched, fetchSize << 2);
#endif
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
#if defined(AMIGA_DIRECTOUT)
    *((volatile mp_uword *) CUSTOM_DMACON) = DMAF_AUDIO;
#   if !defined(AMIGA_PAULA)
    *((volatile mp_uword *) CUSTOM_DMACON2) = DMA2F_AUDIO;
#   endif
#else
    *((volatile mp_uword *) CUSTOM_DMACON) = DMAF_AUD0 | DMAF_AUD1;
#endif
}

void
AudioDriver_Pamela::enableDMA()
{
#if defined(AMIGA_DIRECTOUT)
    *((volatile mp_uword *) CUSTOM_DMACON) = DMAF_SETCLR | DMAF_AUDIO;
#   if !defined(AMIGA_PAULA)
    *((volatile mp_uword *) CUSTOM_DMACON2) = DMAF_SETCLR | DMA2F_AUDIO;
#   endif
#else
    *((volatile mp_uword *) CUSTOM_DMACON) = DMAF_SETCLR | DMAF_AUD0 | DMAF_AUD1;
#endif
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

#if defined(AMIGA_DIRECTOUT)
    for(i = 0; i < MAX_CHANNELS; i++) {
        *((volatile mp_uint32 *) AUDIO_LOCHI(i)) = (mp_uint32) (chanRing[i] + idxRead);
        *((volatile mp_uword *) AUDIO_LENLO(i)) = chunkSize >> AUDIO_LEN_SHIFT;
    }
#else
    *((volatile mp_uint32 *) AUDIO_LOCHI(0)) = (mp_uint32) (samplesLeft + idxRead);
    *((volatile mp_uword *) AUDIO_LENLO(0)) = chunkSize >> AUDIO_LEN_SHIFT;

    *((volatile mp_uint32 *) AUDIO_LOCHI(1)) = (mp_uint32) (samplesRight + idxRead);
    *((volatile mp_uword *) AUDIO_LENLO(1)) = chunkSize >> AUDIO_LEN_SHIFT;
#endif

    idxRead += chunkSize;
    if(idxRead >= ringSize)
        idxRead = 0;

    statAudioBufferReset++;

    custom.intreq = INTF_AUD0;
}

mp_sint32
AudioDriver_Pamela::bufferAudioService(register AudioDriver_Pamela * that __asm("a1"))
{
    return that->bufferAudio();
}

static
mp_uint32 getVerticalBeamPosition() {
    struct vpos {
        mp_uint32 :13;
        mp_uint32 vpos:11;
        mp_uint32 hpos:8;
    };
    return ((struct vpos*)0xdff004)->vpos;
}

mp_sint32
AudioDriver_Pamela::bufferAudio()
{
    int i, j;

    statCountPerSecond++;
    if(statCountPerSecond == 50) {
        statCountPerSecond = 0;

        statAudioBufferResetMedian = (statAudioBufferResetMedian + statAudioBufferReset) >> 1;
        statAudioBufferReset = 0;

        statRingBufferFullMedian = (statRingBufferFullMedian + statRingBufferFull) >> 1;
        statRingBufferFull = 0;
    }

    // Fetch only if we would not write into the block to read
    mp_sint32 idxDist;
    if(idxRead <= idxWrite)
        idxDist = idxWrite - idxRead;
    else
        idxDist = (ringSize - idxRead) + idxWrite;

    if(idxDist >= fetchSize) {
        mp_uint32 vpos = getVerticalBeamPosition();

#if defined(AMIGA_DIRECTOUT)
        if (isMixerActive()) {
#if defined(AMIGA_PAULA)
            mixer->mixerHandler(NULL, MAX_CHANNELS, chanFetch);

            for(i = 0; i < MAX_CHANNELS; i++) {
                mp_sword * s = chanFetch[i];
                mp_smptype * d = chanRing[i] + idxWrite;

                for(j = 0; j < fetchSize; j++) {
                    *(d++) = *(s++) >> 8;
                }
            }
#   else
            for(i = 0; i < MAX_CHANNELS; i++) {
                chanRingPtrs[i] = chanRing[i] + idxWrite;
            }
            mixer->mixerHandler(NULL, MAX_CHANNELS, chanRingPtrs);
#   endif
        } else {
            for(i = 0; i < MAX_CHANNELS; i++) {
                memset(chanRing[i] + idxWrite, 0, fetchSize * SAMPLE_SIZE);
            }
        }
#else
        mp_sword * f = samplesFetched;
        mp_smptype
            * l = samplesLeft + idxWrite,
            * r = samplesRight + idxWrite;

        if (isMixerActive())
            mixer->mixerHandler(f);
        else
            memset(f, 0, fetchSize << 2);

        // Deinterleave stereo
        for(i = 0; i < fetchSize; i++) {
#   if defined(AMIGA_PAULA)
            *(l++) = *(f++) >> 8;
            *(r++) = *(f++) >> 8;
#   else
            *(l++) = *(f++);
            *(r++) = *(f++);
#   endif
        }
#endif

        idxWrite += fetchSize;
        if(idxWrite >= ringSize)
            idxWrite = 0;

        mp_uint32 mix = (((getVerticalBeamPosition() - vpos) * 1000000) / PAL_LINES) / 10000;
        statVerticalBlankMixMedian = (statVerticalBlankMixMedian + mix) >> 1;
    } else {
        statRingBufferFull++;
    }

    return 1;
}


mp_sint32
AudioDriver_Pamela::initDevice(mp_sint32 bufferSizeInWords, mp_uint32 mixFrequency, MasterMixer* mixer)
{
    int i;

#if DEBUG_DRIVER
    printf("%s\n", __PRETTY_FUNCTION__);
    printf("Requested mix frequency: %ld\n", mixFrequency);
    printf("Requested buffer size: %ld\n", bufferSizeInWords);
    printf("Current INTENAR: %lx\n", custom.intenar);
    printf("Current DMACONR: %lx\n", custom.dmaconr);
#endif

#if defined(AMIGA_PAULA)
    printf("Driver running in Paula mode\n");
    mixFrequency = 22050;
#else
    printf("Driver running in Pamela mode\n");
    mixFrequency = mixFrequency > 44100 ? 44100 : mixFrequency;
#endif

	mp_sint32 res = AudioDriverBase::initDevice(bufferSizeInWords, mixFrequency, mixer);
	if (res < 0)
		return res;

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

    mp_sint32 newBufferSize = alloc(bufferSizeInWords);

#if DEBUG_DRIVER
    printf("Obtained mix frequency: %ld\n", mixFrequency);
    printf("Obtained buffer size: %ld\n", newBufferSize);
#endif

	return newBufferSize;
}

mp_sint32
AudioDriver_Pamela::closeDevice()
{
#if DEBUG_DRIVER
    printf("%s\n", __PRETTY_FUNCTION__);
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
    printf("%s\n", __PRETTY_FUNCTION__);
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
    printf("%s\n", __PRETTY_FUNCTION__);
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
    printf("%s\n", __PRETTY_FUNCTION__);
#endif
    disableDMA();

    return MP_OK;
}

mp_sint32
AudioDriver_Pamela::resume()
{
#if DEBUG_DRIVER
    printf("%s\n", __PRETTY_FUNCTION__);
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
    return 2048;
}
