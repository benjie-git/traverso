/*
Copyright (C) 2026 Ben Levitt
Copyright (C) 2026 Traverso Authors

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

#include "FaacAudioWriter.h"
#include <stdio.h>
#include <faac.h>

#include <QString>
#include <QtGlobal>

RELAYTOOL_FAAC;

// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"


struct FaacAudioWriter::FaacInfo {
#if defined(FAAC_VERSION_MAJOR) && (FAAC_VERSION_MAJOR >= 1)
	faac_encoder* encoder;
	uint32_t frameSamples;
	uint32_t maxOutputBytes;
#else
	faacEncHandle handle;
	unsigned long inputSamples;
	unsigned long maxOutputBytes;
#endif
};


FaacAudioWriter::FaacAudioWriter()
	: AbstractAudioWriter()
{
	m_fid = nullptr;
	m_faacInfo = new FaacInfo();
#if defined(FAAC_VERSION_MAJOR) && (FAAC_VERSION_MAJOR >= 1)
	m_faacInfo->encoder = nullptr;
	m_faacInfo->frameSamples = 1024;
	m_faacInfo->maxOutputBytes = 8192;
#else
	m_faacInfo->handle = nullptr;
	m_faacInfo->inputSamples = 0;
	m_faacInfo->maxOutputBytes = 0;
#endif
	m_buffer = nullptr;
	m_bufferSize = 0;

	// Default settings
	m_bitrate = 192;
	m_quality = 100;
	m_useQuality = false;
	m_objectType = 2; // AAC-LC (LOW)
	m_useTns = 1;
	m_useMidside = 1;
}


FaacAudioWriter::~FaacAudioWriter()
{
	if (m_fid) {
		close_private();
	}
	if (m_buffer) {
		delete [] m_buffer;
	}
	delete m_faacInfo;
}


const char* FaacAudioWriter::get_extension()
{
	return ".m4a";
}


bool FaacAudioWriter::set_format_attribute(const QString& key, const QString& value)
{
	if (key == "bitrate") {
		m_bitrate = value.toInt();
		m_useQuality = false;
		return true;
	}
	else if (key == "quality") {
		m_quality = value.toInt();
		m_useQuality = true;
		return true;
	}
	else if (key == "objectType") {
		if (value == "low" || value == "lc") {
			m_objectType = 2;
			return true;
		}
		else if (value == "he") {
			m_objectType = 5;
			return true;
		}
	}
	else if (key == "tns") {
		m_useTns = (value == "true" || value == "1") ? 1 : 0;
		return true;
	}
	else if (key == "midside") {
		m_useMidside = (value == "true" || value == "1") ? 1 : 0;
		return true;
	}

	return false;
}


bool FaacAudioWriter::open_private()
{
	m_fid = fopen(m_fileName.toUtf8().data(), "wb+");
	if (!m_fid) {
		// PERROR("FaacAudioWriter: Could not open file %s", QS_C(m_fileName));
		return false;
	}

#if defined(FAAC_VERSION_MAJOR) && (FAAC_VERSION_MAJOR >= 1)
	faac_params params;
	faac_status st = faac_params_init(&params);
	if (st != FAAC_OK) {
		// PERROR("FaacAudioWriter: faac_params_init failed: %s", faac_strerror(st));
		fclose(m_fid);
		m_fid = nullptr;
		return false;
	}

	params.sample_rate = m_rate;
	params.num_channels = m_channels;
	params.mpeg_version = FAAC_MPEG4;
	params.object_type = (m_objectType == 5) ? FAAC_OBJ_HE_AAC_V1 : FAAC_OBJ_LOW;
	params.joint_mode = (m_channels > 1 && m_useMidside) ? FAAC_JOINT_MS : FAAC_JOINT_NONE;
	params.use_tns = (m_useTns != 0);
	params.output_format = FAAC_STREAM_ADTS;
	params.input_format = FAAC_INPUT_16BIT;

	if (m_useQuality && m_quality > 0) {
		params.quant_quality = m_quality;
		params.bit_rate = 0;
	} else {
		uint32_t kbps = (m_bitrate > 0) ? m_bitrate : 128;
		params.bit_rate = (kbps * 1000) / m_channels;
		params.quant_quality = 0;
	}

	st = faac_encoder_open(&params, &m_faacInfo->encoder);
	if (st != FAAC_OK || !m_faacInfo->encoder) {
		// PERROR("FaacAudioWriter: faac_encoder_open failed: %s", faac_strerror(st));
		fclose(m_fid);
		m_fid = nullptr;
		return false;
	}

	faac_encoder_info info;
	info.struct_size = sizeof(info);
	st = faac_encoder_get_info(m_faacInfo->encoder, &info);
	if (st != FAAC_OK) {
		// PERROR("FaacAudioWriter: faac_encoder_get_info failed: %s", faac_strerror(st));
		faac_encoder_close(&m_faacInfo->encoder);
		fclose(m_fid);
		m_fid = nullptr;
		return false;
	}

	m_faacInfo->frameSamples = info.frame_samples;
	m_faacInfo->maxOutputBytes = info.max_output_bytes;
#else
	m_faacInfo->handle = faacEncOpen(m_rate, m_channels, &m_faacInfo->inputSamples, &m_faacInfo->maxOutputBytes);
	if (!m_faacInfo->handle) {
		// PERROR("FaacAudioWriter: faacEncOpen failed.");
		fclose(m_fid);
		m_fid = nullptr;
		return false;
	}

	faacEncConfigurationPtr cfg = faacEncGetCurrentConfiguration(m_faacInfo->handle);
	if (!cfg) {
		// PERROR("FaacAudioWriter: faacEncGetCurrentConfiguration failed.");
		faacEncClose(m_faacInfo->handle);
		m_faacInfo->handle = nullptr;
		fclose(m_fid);
		m_fid = nullptr;
		return false;
	}

	cfg->aacObjectType = (m_objectType == 5) ? 5 : LOW;
	cfg->mpegVersion = MPEG4;
	cfg->useTns = m_useTns ? 1 : 0;
	cfg->allowMidside = (m_channels > 1 && m_useMidside) ? 1 : 0;
	cfg->outputFormat = 1; // 1 = ADTS
	cfg->inputFormat = FAAC_INPUT_16BIT;

	if (m_useQuality && m_quality > 0) {
		cfg->quantqual = m_quality;
		cfg->bitRate = 0;
	} else {
		uint32_t kbps = (m_bitrate > 0) ? m_bitrate : 128;
		cfg->bitRate = (kbps * 1000) / m_channels;
		cfg->quantqual = 0;
	}

	if (!faacEncSetConfiguration(m_faacInfo->handle, cfg)) {
		// PERROR("FaacAudioWriter: faacEncSetConfiguration failed.");
		faacEncClose(m_faacInfo->handle);
		m_faacInfo->handle = nullptr;
		fclose(m_fid);
		m_fid = nullptr;
		return false;
	}
#endif

	return true;
}


nframes_t FaacAudioWriter::write_private(void* buffer, nframes_t frameCount)
{
	if (!buffer || frameCount == 0) {
		return 0;
	}

	if (m_bufferSize < (long)m_faacInfo->maxOutputBytes) {
		delete [] m_buffer;
		m_bufferSize = m_faacInfo->maxOutputBytes;
		m_buffer = new char[m_bufferSize];
	}

	const int16_t* pcmData = static_cast<const int16_t*>(buffer);
	uint32_t totalSamples = (uint32_t)frameCount * m_channels;

#if defined(FAAC_VERSION_MAJOR) && (FAAC_VERSION_MAJOR >= 1)
	uint32_t maxChunkSamples = m_faacInfo->frameSamples * m_channels;
	uint32_t samplesProcessed = 0;

	while (samplesProcessed < totalSamples) {
		uint32_t chunk = qMin(totalSamples - samplesProcessed, maxChunkSamples);
		uint32_t bytesWritten = 0;
		faac_status status = faac_encoder_encode(m_faacInfo->encoder,
							 pcmData + samplesProcessed,
							 chunk,
							 reinterpret_cast<uint8_t*>(m_buffer),
							 m_bufferSize,
							 &bytesWritten);
		if (status < 0) {
			// PERROR("FaacAudioWriter: faac_encoder_encode failed: %s", faac_strerror(status));
			return 0;
		}

		if (bytesWritten > 0) {
			if (fwrite(m_buffer, 1, bytesWritten, m_fid) != bytesWritten) {
				// PERROR("FaacAudioWriter: fwrite failed.");
				return 0;
			}
		}

		samplesProcessed += chunk;
	}
#else
	unsigned long maxChunkSamples = m_faacInfo->inputSamples;
	unsigned long samplesProcessed = 0;

	while (samplesProcessed < totalSamples) {
		unsigned long chunk = qMin((unsigned long)(totalSamples - samplesProcessed), maxChunkSamples);
		int bytesWritten = faacEncEncode(m_faacInfo->handle,
						(int32_t*)(pcmData + samplesProcessed),
						chunk,
						reinterpret_cast<unsigned char*>(m_buffer),
						m_bufferSize);
		if (bytesWritten < 0) {
			// PERROR("FaacAudioWriter: faacEncEncode failed.");
			return 0;
		}

		if (bytesWritten > 0) {
			if (fwrite(m_buffer, 1, bytesWritten, m_fid) != (size_t)bytesWritten) {
				// PERROR("FaacAudioWriter: fwrite failed.");
				return 0;
			}
		}

		samplesProcessed += chunk;
	}
#endif

	return frameCount;
}


bool FaacAudioWriter::close_private()
{
	bool success = true;

	if (m_bufferSize < (long)m_faacInfo->maxOutputBytes) {
		delete [] m_buffer;
		m_bufferSize = m_faacInfo->maxOutputBytes;
		m_buffer = new char[m_bufferSize];
	}

#if defined(FAAC_VERSION_MAJOR) && (FAAC_VERSION_MAJOR >= 1)
	if (m_faacInfo->encoder) {
		uint32_t bytesWritten = 0;
		do {
			bytesWritten = 0;
			faac_status status = faac_encoder_encode(m_faacInfo->encoder,
								 nullptr,
								 0,
								 reinterpret_cast<uint8_t*>(m_buffer),
								 m_bufferSize,
								 &bytesWritten);
			if (status < 0) {
				// PERROR("FaacAudioWriter: faac_encoder_encode flush failed: %s", faac_strerror(status));
				success = false;
				break;
			}
			if (bytesWritten > 0) {
				if (fwrite(m_buffer, 1, bytesWritten, m_fid) != bytesWritten) {
					// PERROR("FaacAudioWriter: flush fwrite failed.");
					success = false;
					break;
				}
			}
		} while (bytesWritten > 0);

		faac_encoder_close(&m_faacInfo->encoder);
		m_faacInfo->encoder = nullptr;
	}
#else
	if (m_faacInfo->handle) {
		int bytesWritten = 0;
		do {
			bytesWritten = faacEncEncode(m_faacInfo->handle,
						     nullptr,
						     0,
						     reinterpret_cast<unsigned char*>(m_buffer),
						     m_bufferSize);
			if (bytesWritten < 0) {
				// PERROR("FaacAudioWriter: faacEncEncode flush failed.");
				success = false;
				break;
			}
			if (bytesWritten > 0) {
				if (fwrite(m_buffer, 1, bytesWritten, m_fid) != (size_t)bytesWritten) {
					// PERROR("FaacAudioWriter: flush fwrite failed.");
					success = false;
					break;
				}
			}
		} while (bytesWritten > 0);

		faacEncClose(m_faacInfo->handle);
		m_faacInfo->handle = nullptr;
	}
#endif

	if (m_fid) {
		fclose(m_fid);
		m_fid = nullptr;
	}

	return success;
}
