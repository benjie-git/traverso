/*
Copyright (C) 2026 Traverso Team

This file is part of Traverso

Traverso is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA.

*/

#include "FaadAudioReader.h"
#include "Utils.h"

#include <QFile>
#include <QVector>
#include <algorithm>

#define MINIMP4_IMPLEMENTATION
#include "minimp4.h"

extern "C" {
#include <neaacdec.h>
}

RELAYTOOL_FAAD;

// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"

static const uint OVERFLOW_BUFFER_FRAMES = 8192;

static int mp4_read_callback(int64_t offset, void *buffer, size_t size, void *token)
{
	QFile *file = static_cast<QFile*>(token);
	if (!file || !file->seek(offset)) {
		return 1;
	}
	qint64 bytesRead = file->read(reinterpret_cast<char*>(buffer), size);
	return (bytesRead == static_cast<qint64>(size)) ? 0 : 1;
}

class FaadAudioReader::FaadDecoderPrivate
{
public:
	FaadDecoderPrivate()
		: hDecoder(nullptr)
		, isMp4(false)
		, audioTrack(-1)
		, sampleCount(0)
		, currentPacket(0)
		, currentAdtsFrame(0)
		, overflowBuffers(nullptr)
		, overflowStart(0)
		, overflowSize(0)
		, overflowBufferSize(0)
	{
		memset(&demux, 0, sizeof(demux));
	}

	~FaadDecoderPrivate()
	{
		cleanup();
	}

	void cleanup()
	{
		if (hDecoder) {
			NeAACDecClose(hDecoder);
			hDecoder = nullptr;
		}

		if (isMp4) {
			MP4D_close(&demux);
			memset(&demux, 0, sizeof(demux));
			isMp4 = false;
		}

		if (file.isOpen()) {
			file.close();
		}

		samplePcmOffsets.clear();
		adtsFrameOffsets.clear();
		adtsFrameSizes.clear();

		sampleCount = 0;
		currentPacket = 0;
		currentAdtsFrame = 0;
		audioTrack = -1;
	}

	QByteArray readBytes(qint64 offset, int size)
	{
		if (size <= 0) {
			return QByteArray();
		}
		if (!file.seek(offset)) {
			return QByteArray();
		}
		return file.read(size);
	}

	QFile file;
	NeAACDecHandle hDecoder;
	bool isMp4;
	MP4D_demux_t demux;
	int audioTrack;
	unsigned int sampleCount;
	QVector<nframes_t> samplePcmOffsets;

	// For ADTS fallback
	QVector<qint64> adtsFrameOffsets;
	QVector<int> adtsFrameSizes;

	unsigned int currentPacket;
	unsigned int currentAdtsFrame;

	audio_sample_t** overflowBuffers;
	nframes_t overflowStart;
	nframes_t overflowSize;
	uint overflowBufferSize;
};

FaadAudioReader::FaadAudioReader(const QString& filename)
	: AbstractAudioReader(filename)
	, d(new FaadDecoderPrivate())
{
	if (!libfaad_is_present) {
		return;
	}

	if (!initDecoderInternal()) {
		d->cleanup();
	}
}

FaadAudioReader::~FaadAudioReader()
{
	clear_buffers();
	delete d;
}

void FaadAudioReader::create_buffers()
{
	if (d->overflowBuffers) {
		return;
	}

	d->overflowBufferSize = OVERFLOW_BUFFER_FRAMES;
	d->overflowBuffers = new audio_sample_t*[m_channels];
	for (uint i = 0; i < m_channels; ++i) {
		d->overflowBuffers[i] = new audio_sample_t[d->overflowBufferSize];
		memset(d->overflowBuffers[i], 0, d->overflowBufferSize * sizeof(audio_sample_t));
	}
	d->overflowStart = 0;
	d->overflowSize = 0;
}

void FaadAudioReader::clear_buffers()
{
	if (d && d->overflowBuffers) {
		for (uint i = 0; i < m_channels; ++i) {
			delete [] d->overflowBuffers[i];
		}
		delete [] d->overflowBuffers;
		d->overflowBuffers = nullptr;
		d->overflowStart = 0;
		d->overflowSize = 0;
		d->overflowBufferSize = 0;
	}
}

bool FaadAudioReader::can_decode(const QString& filename)
{
	if (!libfaad_is_present) {
		return false;
	}

	QFile f(filename);
	if (!f.open(QIODevice::ReadOnly)) {
		return false;
	}

	char header[12];
	qint64 readBytes = f.read(header, 12);
	if (readBytes < 12) {
		return false;
	}

	// Filter out other formats to avoid false positives
	if (!qstrncmp(header, "RIFF", 4) || !qstrncmp(header, "fLaC", 4) ||
	    !qstrncmp(header, "OggS", 4) || !qstrncmp(header, "wvpk", 4)) {
		return false;
	}

	// 1. Check if valid MP4 / M4A container
	f.seek(0);
	MP4D_demux_t demux;
	int mp4Ok = MP4D_open(&demux, mp4_read_callback, &f, f.size());
	if (mp4Ok) {
		bool hasAudio = false;
		for (unsigned int i = 0; i < demux.track_count; ++i) {
			unsigned int obj = demux.track[i].object_type_indication;
			unsigned int handler = demux.track[i].handler_type;
			if (handler == 0x736f756e || // 'soun'
			    obj == MP4_OBJECT_TYPE_AUDIO_ISO_IEC_14496_3 ||
			    obj == MP4_OBJECT_TYPE_AUDIO_ISO_IEC_13818_7_MAIN_PROFILE ||
			    obj == MP4_OBJECT_TYPE_AUDIO_ISO_IEC_13818_7_LC_PROFILE ||
			    obj == MP4_OBJECT_TYPE_AUDIO_ISO_IEC_13818_7_SSR_PROFILE ||
			    demux.track[i].SampleDescription.audio.channelcount > 0) {
				hasAudio = true;
				break;
			}
		}
		MP4D_close(&demux);
		if (hasAudio) {
			return true;
		}
	}

	// 2. Check if valid ADTS AAC stream
	f.seek(0);
	QByteArray buf = f.read(4096);
	if (buf.size() >= 7) {
		const unsigned char* p = reinterpret_cast<const unsigned char*>(buf.constData());
		if (p[0] == 0xFF && (p[1] & 0xF0) == 0xF0) {
			NeAACDecHandle h = NeAACDecOpen();
			if (h) {
				NeAACDecConfigurationPtr config = NeAACDecGetCurrentConfiguration(h);
				config->outputFormat = FAAD_FMT_FLOAT;
				NeAACDecSetConfiguration(h, config);

				unsigned long rate = 0;
				unsigned char ch = 0;
				long initRes = NeAACDecInit(h, const_cast<unsigned char*>(p), buf.size(), &rate, &ch);
				NeAACDecClose(h);
				if (initRes >= 0 && rate > 0 && ch > 0) {
					return true;
				}
			}
		}
	}

	return false;
}

bool FaadAudioReader::initDecoderInternal()
{
	d->cleanup();

	d->file.setFileName(m_fileName);
	if (!d->file.open(QIODevice::ReadOnly)) {
		return false;
	}

	// 1. Attempt MP4 demuxing with minimp4
	int mp4Ok = MP4D_open(&d->demux, mp4_read_callback, &d->file, d->file.size());
	if (mp4Ok) {
		for (unsigned int i = 0; i < d->demux.track_count; ++i) {
			unsigned int obj = d->demux.track[i].object_type_indication;
			unsigned int handler = d->demux.track[i].handler_type;
			if (handler == 0x736f756e || // 'soun'
			    obj == MP4_OBJECT_TYPE_AUDIO_ISO_IEC_14496_3 ||
			    obj == MP4_OBJECT_TYPE_AUDIO_ISO_IEC_13818_7_MAIN_PROFILE ||
			    obj == MP4_OBJECT_TYPE_AUDIO_ISO_IEC_13818_7_LC_PROFILE ||
			    obj == MP4_OBJECT_TYPE_AUDIO_ISO_IEC_13818_7_SSR_PROFILE ||
			    d->demux.track[i].SampleDescription.audio.channelcount > 0) {
				d->audioTrack = static_cast<int>(i);
				break;
			}
		}

		if (d->audioTrack >= 0) {
			d->isMp4 = true;
			MP4D_track_t *track = &d->demux.track[d->audioTrack];
			d->sampleCount = track->sample_count;

			d->hDecoder = NeAACDecOpen();
			if (!d->hDecoder) {
				return false;
			}

			NeAACDecConfigurationPtr config = NeAACDecGetCurrentConfiguration(d->hDecoder);
			config->outputFormat = FAAD_FMT_FLOAT;
			config->dontUpSampleImplicitSBR = 0;
			NeAACDecSetConfiguration(d->hDecoder, config);

			unsigned long rate = 0;
			unsigned char ch = 0;

			if (track->dsi && track->dsi_bytes > 0) {
				char initRes = NeAACDecInit2(d->hDecoder, track->dsi, track->dsi_bytes, &rate, &ch);
				if (initRes < 0) {
					return false;
				}
			} else if (d->sampleCount > 0) {
				unsigned int fb = 0, ts = 0, dur = 0;
				MP4D_file_offset_t off = MP4D_frame_offset(&d->demux, d->audioTrack, 0, &fb, &ts, &dur);
				QByteArray firstPkt = d->readBytes(off, fb);
				long initRes = NeAACDecInit(d->hDecoder, reinterpret_cast<unsigned char*>(firstPkt.data()), fb, &rate, &ch);
				if (initRes < 0) {
					return false;
				}
			}

			m_rate = static_cast<uint>(rate > 0 ? rate : track->SampleDescription.audio.samplerate_hz);
			m_channels = static_cast<uint>(ch > 0 ? ch : track->SampleDescription.audio.channelcount);

			if (m_rate == 0 || m_channels == 0) {
				return false;
			}

			// Precalculate sample PCM offsets
			d->samplePcmOffsets.resize(d->sampleCount + 1);
			d->samplePcmOffsets[0] = 0;

			unsigned int timescale = track->timescale ? track->timescale : m_rate;
			for (unsigned int i = 0; i < d->sampleCount; ++i) {
				unsigned int fb = 0, ts = 0, dur = 0;
				MP4D_frame_offset(&d->demux, d->audioTrack, i, &fb, &ts, &dur);
				nframes_t pcm_pos = static_cast<nframes_t>(((double)ts / timescale) * m_rate + 0.5);
				d->samplePcmOffsets[i] = pcm_pos;
			}

			uint64_t totalDur = ((uint64_t)track->duration_hi << 32) | track->duration_lo;
			if (totalDur > 0) {
				m_nframes = static_cast<nframes_t>(((double)totalDur / timescale) * m_rate + 0.5);
			} else {
				m_nframes = d->sampleCount * 1024;
			}
			d->samplePcmOffsets[d->sampleCount] = m_nframes;

			m_length = TimeRef(m_nframes, m_rate);
			m_readPos = 0;
			d->currentPacket = 0;
			return true;
		}
	}

	// 2. Fallback: ADTS AAC stream parsing
	d->cleanup();
	d->file.setFileName(m_fileName);
	if (!d->file.open(QIODevice::ReadOnly)) {
		return false;
	}

	qint64 fsize = d->file.size();
	qint64 offset = 0;
	while (offset + 7 <= fsize) {
		d->file.seek(offset);
		unsigned char hdr[7];
		if (d->file.read(reinterpret_cast<char*>(hdr), 7) != 7) {
			break;
		}
		if (hdr[0] != 0xFF || (hdr[1] & 0xF0) != 0xF0) {
			offset++;
			continue;
		}
		int frameLen = ((hdr[3] & 0x03) << 11) | (hdr[4] << 3) | ((hdr[5] & 0xE0) >> 5);
		if (frameLen < 7 || offset + frameLen > fsize) {
			offset++;
			continue;
		}
		d->adtsFrameOffsets.append(offset);
		d->adtsFrameSizes.append(frameLen);
		offset += frameLen;
	}

	if (d->adtsFrameOffsets.isEmpty()) {
		return false;
	}

	d->isMp4 = false;
	d->hDecoder = NeAACDecOpen();
	if (!d->hDecoder) {
		return false;
	}

	NeAACDecConfigurationPtr config = NeAACDecGetCurrentConfiguration(d->hDecoder);
	config->outputFormat = FAAD_FMT_FLOAT;
	NeAACDecSetConfiguration(d->hDecoder, config);

	QByteArray firstFrame = d->readBytes(d->adtsFrameOffsets[0], d->adtsFrameSizes[0]);
	unsigned long rate = 0;
	unsigned char ch = 0;
	long initRes = NeAACDecInit(d->hDecoder, reinterpret_cast<unsigned char*>(firstFrame.data()), firstFrame.size(), &rate, &ch);
	if (initRes < 0 || rate == 0 || ch == 0) {
		return false;
	}

	m_rate = static_cast<uint>(rate);
	m_channels = static_cast<uint>(ch);
	m_nframes = static_cast<nframes_t>(d->adtsFrameOffsets.size() * 1024);
	m_length = TimeRef(m_nframes, m_rate);
	m_readPos = 0;
	d->currentAdtsFrame = 0;
	return true;
}

bool FaadAudioReader::seek_private(nframes_t start)
{
	Q_ASSERT(d);

	if (start >= m_nframes) {
		return false;
	}

	if (!d->hDecoder) {
		if (!initDecoderInternal()) {
			return false;
		}
	}

	if (!d->overflowBuffers) {
		create_buffers();
	}

	d->overflowStart = 0;
	d->overflowSize = 0;

	if (d->isMp4) {
		if (d->sampleCount == 0) {
			return false;
		}

		auto it = std::upper_bound(d->samplePcmOffsets.begin(), d->samplePcmOffsets.end(), start);
		int targetPkt = std::max(0, static_cast<int>(std::distance(d->samplePcmOffsets.begin(), it) - 1));
		if (targetPkt >= static_cast<int>(d->sampleCount)) {
			targetPkt = static_cast<int>(d->sampleCount) - 1;
		}

		int prerollPkt = std::max(0, targetPkt - 2);

		NeAACDecPostSeekReset(d->hDecoder, 0);

		// Preroll warmup
		for (int j = prerollPkt; j < targetPkt; ++j) {
			unsigned int fb = 0, ts = 0, dur = 0;
			MP4D_file_offset_t off = MP4D_frame_offset(&d->demux, d->audioTrack, j, &fb, &ts, &dur);
			QByteArray pktData = d->readBytes(off, fb);
			NeAACDecFrameInfo info;
			NeAACDecDecode(d->hDecoder, &info, reinterpret_cast<unsigned char*>(pktData.data()), fb);
		}

		// Decode target packet
		unsigned int fb = 0, ts = 0, dur = 0;
		MP4D_file_offset_t off = MP4D_frame_offset(&d->demux, d->audioTrack, targetPkt, &fb, &ts, &dur);
		QByteArray pktData = d->readBytes(off, fb);
		NeAACDecFrameInfo info;
		void* pcm = NeAACDecDecode(d->hDecoder, &info, reinterpret_cast<unsigned char*>(pktData.data()), fb);
		d->currentPacket = targetPkt + 1;

		if (info.error == 0 && info.samples > 0 && m_channels > 0) {
			nframes_t decodedFrames = info.samples / m_channels;
			nframes_t offsetInPkt = (start > d->samplePcmOffsets[targetPkt]) ? (start - d->samplePcmOffsets[targetPkt]) : 0;
			if (offsetInPkt < decodedFrames) {
				nframes_t rem = decodedFrames - offsetInPkt;
				float* samples = static_cast<float*>(pcm);
				for (uint c = 0; c < m_channels; ++c) {
					for (nframes_t f = 0; f < rem; ++f) {
						d->overflowBuffers[c][f] = samples[(offsetInPkt + f) * m_channels + c];
					}
				}
				d->overflowStart = 0;
				d->overflowSize = rem;
			}
		}

		return true;
	} else {
		// ADTS mode
		if (d->adtsFrameOffsets.isEmpty()) {
			return false;
		}

		int targetFrame = static_cast<int>(start / 1024);
		if (targetFrame >= d->adtsFrameOffsets.size()) {
			targetFrame = d->adtsFrameOffsets.size() - 1;
		}

		int prerollFrame = std::max(0, targetFrame - 2);

		NeAACDecPostSeekReset(d->hDecoder, 0);

		// Preroll warmup
		for (int j = prerollFrame; j < targetFrame; ++j) {
			QByteArray frameData = d->readBytes(d->adtsFrameOffsets[j], d->adtsFrameSizes[j]);
			NeAACDecFrameInfo info;
			NeAACDecDecode(d->hDecoder, &info, reinterpret_cast<unsigned char*>(frameData.data()), frameData.size());
		}

		// Decode target frame
		QByteArray frameData = d->readBytes(d->adtsFrameOffsets[targetFrame], d->adtsFrameSizes[targetFrame]);
		NeAACDecFrameInfo info;
		void* pcm = NeAACDecDecode(d->hDecoder, &info, reinterpret_cast<unsigned char*>(frameData.data()), frameData.size());
		d->currentAdtsFrame = targetFrame + 1;

		if (info.error == 0 && info.samples > 0 && m_channels > 0) {
			nframes_t decodedFrames = info.samples / m_channels;
			nframes_t offsetInFrame = start % 1024;
			if (offsetInFrame < decodedFrames) {
				nframes_t rem = decodedFrames - offsetInFrame;
				float* samples = static_cast<float*>(pcm);
				for (uint c = 0; c < m_channels; ++c) {
					for (nframes_t f = 0; f < rem; ++f) {
						d->overflowBuffers[c][f] = samples[(offsetInFrame + f) * m_channels + c];
					}
				}
				d->overflowStart = 0;
				d->overflowSize = rem;
			}
		}

		return true;
	}
}

nframes_t FaadAudioReader::read_private(DecodeBuffer* buffer, nframes_t frameCount)
{
	audio_sample_t** dst = buffer->destination;
	nframes_t outputPos = 0;

	if (!d->overflowBuffers) {
		create_buffers();
	}

	// 1. Drain existing overflow samples
	if (d->overflowSize > 0) {
		nframes_t toCopy = std::min(d->overflowSize, frameCount);
		for (uint c = 0; c < m_channels; ++c) {
			memcpy(dst[c] + outputPos, d->overflowBuffers[c] + d->overflowStart, toCopy * sizeof(audio_sample_t));
		}
		d->overflowStart += toCopy;
		d->overflowSize -= toCopy;
		outputPos += toCopy;

		if (outputPos >= frameCount) {
			return outputPos;
		}
	}

	// 2. Decode subsequent frames/packets
	while (outputPos < frameCount) {
		QByteArray packetData;

		if (d->isMp4) {
			if (d->currentPacket >= d->sampleCount) {
				break; // EOF
			}
			unsigned int fb = 0, ts = 0, dur = 0;
			MP4D_file_offset_t off = MP4D_frame_offset(&d->demux, d->audioTrack, d->currentPacket, &fb, &ts, &dur);
			packetData = d->readBytes(off, fb);
			d->currentPacket++;
		} else {
			if (d->currentAdtsFrame >= static_cast<unsigned int>(d->adtsFrameOffsets.size())) {
				break; // EOF
			}
			packetData = d->readBytes(d->adtsFrameOffsets[d->currentAdtsFrame], d->adtsFrameSizes[d->currentAdtsFrame]);
			d->currentAdtsFrame++;
		}

		if (packetData.isEmpty()) {
			break;
		}

		NeAACDecFrameInfo info;
		void* pcm = NeAACDecDecode(d->hDecoder, &info, reinterpret_cast<unsigned char*>(packetData.data()), packetData.size());

		if (info.error > 0) {
			continue;
		}
		if (info.samples == 0) {
			continue;
		}

		nframes_t decodedFrames = info.samples / m_channels;
		float* samples = static_cast<float*>(pcm);
		nframes_t space = frameCount - outputPos;

		if (decodedFrames <= space) {
			for (uint c = 0; c < m_channels; ++c) {
				for (nframes_t f = 0; f < decodedFrames; ++f) {
					dst[c][outputPos + f] = samples[f * m_channels + c];
				}
			}
			outputPos += decodedFrames;
		} else {
			// Write what fits into destination buffer
			for (uint c = 0; c < m_channels; ++c) {
				for (nframes_t f = 0; f < space; ++f) {
					dst[c][outputPos + f] = samples[f * m_channels + c];
				}
			}
			outputPos += space;

			// Store remaining samples in overflow buffer
			nframes_t rem = decodedFrames - space;
			for (uint c = 0; c < m_channels; ++c) {
				for (nframes_t f = 0; f < rem; ++f) {
					d->overflowBuffers[c][f] = samples[(space + f) * m_channels + c];
				}
			}
			d->overflowStart = 0;
			d->overflowSize = rem;
			break;
		}
	}

	return outputPos;
}
