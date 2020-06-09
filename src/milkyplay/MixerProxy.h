/*
 * Copyright (c) 2020, neoman
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors
 *   may be used to endorse or promote products derived from this software
 *   without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __MIXER_DEVICE_H__
#define __MIXER_DEVICE_H__

#include "MilkyPlayTypes.h"

#define MAX_DIRECTOUT_CHANNELS 16

//
// Intermediate structure as glue between the mixer and the driver
// This one can be quite interdisciplinary.
//
// -> @todo this is not clean yet.
//
class MixerProxy
{
public:
	enum ProcessingType {
		MixDown,
		DirectOut,
		HardwareOut
	};

	class Processor {
	public:
	};

protected:
	mp_uint32 				numChannels;
	mp_uint32 				bufferSize;
	mp_sword **				buffers;
	Processor *             processor;

public:
	virtual bool 			lock() { return false; }
	virtual bool			unlock() { return true; }
	virtual void            setBuffer(mp_uint32 idx, mp_sword * buffer) { }
	virtual void            advanceBuffer(mp_uint32 idx, mp_uint32 offset) { buffers[idx] += offset; }
	virtual mp_sword *      getBuffer(mp_uint32 idx) const { return buffers[idx]; }
	virtual ProcessingType 	getProcessingType() const = 0;
	virtual mp_uint32       getNumChannels() const = 0;
	virtual Processor *     getProcessor() const { return processor; };

	MixerProxy(mp_uint32 numChannels, mp_uint32 bufferSize, Processor * processor);
	virtual ~MixerProxy() {};
};

class MixerProxyDirectOut : public MixerProxy
{
private:
public:
	virtual bool 			lock();
	virtual ProcessingType	getProcessingType() const { return DirectOut; }
	virtual mp_uint32       getNumChannels() const { return numChannels; }
	virtual void            setBuffer(mp_uint32 idx, mp_sword * buffer);

	MixerProxyDirectOut(mp_uint32 numChannels, mp_uint32 bufferSize, Processor * processor);
	virtual ~MixerProxyDirectOut();
};

class MixerProxyHardwareOut : public MixerProxy
{
private:
public:
	virtual bool 			lock();
	virtual ProcessingType	getProcessingType() const { return HardwareOut; }
	virtual mp_uint32       getNumChannels() const { return numChannels; }

	MixerProxyHardwareOut(mp_uint32 numChannels, mp_uint32 bufferSize, Processor * processor);
	virtual ~MixerProxyHardwareOut() {}
};

#endif
