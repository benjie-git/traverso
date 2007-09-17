/*
Copyright (C) 2005-2007 Remon Sijrier

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

#include "libtraversocore.h"

#include "Peak.h"

#include "PeakDataReader.h"
#include "ResampleAudioReader.h"
#include "ReadSource.h"
#include "ResourcesManager.h"
#include "defines.h"
#include "Mixer.h"
#include <QFileInfo>
#include <QDateTime>
#include <QMutexLocker>

#include "Debugger.h"

/* Store for each zoomStep the upper and lower sample to hard disk as a
* peak_data_t. The top-top resolution is then 512 pixels, which should do
* Painting the waveform will be as simple as painting a line starting from the
* lower value to the upper value.
*/

const int Peak::MAX_DB_VALUE			= 120;
const int SAVING_ZOOM_FACTOR 			= 6;
const int Peak::MAX_ZOOM_USING_SOURCEFILE	= SAVING_ZOOM_FACTOR - 1;

#define NORMALIZE_CHUNK_SIZE	10000
#define PEAKFILE_MAJOR_VERSION	1
#define PEAKFILE_MINOR_VERSION	0

int Peak::zoomStep[] = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096,
				8192, 16384, 32768, 65536, 131072, 262144, 524288, 1048576};

Peak::Peak(AudioSource* source)
{
	PENTERCONS;
	
	peaksAvailable = permanentFailure = interuptPeakBuild = false;
	
	QString sourcename = source->get_name();
	QString path = pm().get_project()->get_root_dir() + "/peakfiles/";
	
	for (uint chan = 0; chan < source->get_channel_count(); ++ chan) {
		ChannelData* data = new Peak::ChannelData;
		
		data->fileName = sourcename + "-ch" + QByteArray::number(chan) + ".peak";
		data->fileName.prepend(path);
		data->file = 0;
		data->normFile = 0;
		data->pd = 0;
		
		m_channelData.append(data);
	}
	
	ReadSource* rs = qobject_cast<ReadSource*>(source);
	
	if (rs) {
		// This Peak object was created by AudioClip, meant for reading peak data
		m_source = resources_manager()->get_readsource(rs->get_id());
		m_source->set_output_rate(44100);
	} else {
		// No ReadSource object? Then it's created by WriteSource for on the fly
		// peak data creation, no m_source needed!
		m_source = 0;
	}
}

Peak::~Peak()
{
	PENTERDES;
	
	if (m_source) {
		delete m_source;
	}
	
	foreach(ChannelData* data, m_channelData) {
		if (data->file) {
			fclose(data->file);
		}
		
		if (data->normFile) {
			fclose(data->normFile);
			QFile::remove(data->normFileName);
		}
		
		delete data;
	}
}

void Peak::close()
{
	pp().free_peak(this);
}

int Peak::read_header()
{
	PENTER;
	
	Q_ASSERT(m_source);
	
	foreach(ChannelData* data, m_channelData) {
		data->file = fopen(data->fileName.toUtf8().data(),"rb");
		
		if (! data->file) {
			PERROR("Couldn't open peak file for reading! (%s)", data->fileName.toAscii().data());
			return -1;
		}
		
		QFileInfo file(m_source->get_filename());
		QFileInfo peakFile(data->fileName);
		
		QDateTime fileModTime = file.lastModified();
		QDateTime peakModTime = peakFile.lastModified();
		
		if (fileModTime > peakModTime) {
			PERROR("Source and Peak file modification time do not match");
			printf("SourceFile modification time is %s\n", fileModTime.toString().toAscii().data());
			printf("PeakFile modification time is %s\n", peakModTime.toString().toAscii().data());
			return -1;
		}
		
		
		fseek(data->file, 0, SEEK_SET);
	
		fread(data->headerdata.label, sizeof(data->headerdata.label), 1, data->file);
		fread(data->headerdata.version, sizeof(data->headerdata.version), 1, data->file);
	
		if (	(data->headerdata.label[0]!='T') ||
			(data->headerdata.label[1]!='R') ||
			(data->headerdata.label[2]!='A') ||
			(data->headerdata.label[3]!='V') ||
			(data->headerdata.label[4]!='P') ||
			(data->headerdata.label[5]!='F') ||
			(data->headerdata.version[0] != PEAKFILE_MAJOR_VERSION) ||
			(data->headerdata.version[1] != PEAKFILE_MINOR_VERSION)) {
				printf("This file either isn't a Traverso Peak file, or the version doesn't match!\n");
				fclose(data->file);
				data->file = 0;
				return -1;
		}
		
		fread(data->headerdata.peakDataLevelOffsets, sizeof(data->headerdata.peakDataLevelOffsets), 1, data->file);
		fread(data->headerdata.peakDataSizeForLevel, sizeof(data->headerdata.peakDataSizeForLevel), 1, data->file);
		fread(&data->headerdata.normValuesDataOffset, sizeof(data->headerdata.normValuesDataOffset), 1, data->file);
		fread(&data->headerdata.peakDataOffset, sizeof(data->headerdata.peakDataOffset), 1, data->file);
		
		data->peakreader = new PeakDataReader(data->fileName);
		data->peakdataDecodeBuffer = new DecodeBuffer;
	}
	
	m_peakdataDecodeBuffer = new DecodeBuffer;

	peaksAvailable = true;
		
	return 1;
}

int Peak::write_header(ChannelData* data)
{
	PENTER;
	
	fseek (data->file, 0, SEEK_SET);

	data->headerdata.label[0] = 'T';
	data->headerdata.label[1] = 'R';
	data->headerdata.label[2] = 'A';
	data->headerdata.label[3] = 'V';
	data->headerdata.label[4] = 'P';
	data->headerdata.label[5] = 'F';
	data->headerdata.version[0] = PEAKFILE_MAJOR_VERSION;
	data->headerdata.version[1] = PEAKFILE_MINOR_VERSION;
	
	fwrite(data->headerdata.label, sizeof(data->headerdata.label), 1, data->file);
	fwrite(data->headerdata.version, sizeof(data->headerdata.version), 1, data->file);
	fwrite(data->headerdata.peakDataLevelOffsets, sizeof(data->headerdata.peakDataLevelOffsets), 1, data->file);
	fwrite(data->headerdata.peakDataSizeForLevel, sizeof(data->headerdata.peakDataSizeForLevel), 1, data->file);
	fwrite((void*) &data->headerdata.normValuesDataOffset, sizeof(data->headerdata.normValuesDataOffset), 1, data->file);
	fwrite((void*) &data->headerdata.peakDataOffset, sizeof(data->headerdata.peakDataOffset), 1, data->file);
	
	return 1;
}


void Peak::start_peak_loading()
{
	pp().queue_task(this);
}


static int
cnt_bits(unsigned long val, int & highbit)
{
	int cnt = 0;
	highbit = 0;
	while (val) {
		if (val & 1) cnt++;
		val>>=1;
		highbit++;
	}
	return cnt;
}

// returns the next power of two greater or equal to val
static unsigned long
nearest_power_of_two(unsigned long val)
{
	int highbit;
	if (cnt_bits(val, highbit) > 1) {
		return 1<<highbit;
	}
	return val;
}


int Peak::calculate_peaks(int chan, float** buffer, int zoomLevel, nframes_t startPos, int pixelcount)
{
	PENTER3;
	
	if (permanentFailure) {
		return PERMANENT_FAILURE;
	}
	
	if(!peaksAvailable) {
		if (read_header() < 0) {
			return NO_PEAK_FILE;
		}
	}
	
	if (pixelcount <= 0) {
		return 1;
	}
	
	ChannelData* data = m_channelData.at(chan);
	
// #define profile

#if defined (profile)
	trav_time_t starttime = get_microseconds();
#endif
	
	// Macro view mode
	if ( zoomLevel > MAX_ZOOM_USING_SOURCEFILE) {
		
		int offset = (startPos / zoomStep[zoomLevel]) * 2;
		
		// Check if this zoom level has as many data as requested.
		if ( (pixelcount + offset) > data->headerdata.peakDataSizeForLevel[zoomLevel - SAVING_ZOOM_FACTOR]) {
			// YES we know that sometimes we ommit the very last 'pixel' to avoid painting artifacts...
//  			PERROR("pixelcount exceeds available data size! (pixelcount is: %d, available is %d", pixelcount, data->headerdata.peakDataSizeForLevel[zoomLevel - SAVING_ZOOM_FACTOR] - offset); 
			pixelcount = data->headerdata.peakDataSizeForLevel[zoomLevel - SAVING_ZOOM_FACTOR] - offset;
		}
		
		nframes_t readposition = data->headerdata.peakDataLevelOffsets[zoomLevel - SAVING_ZOOM_FACTOR] + offset;
		int read = data->peakreader->read_from(data->peakdataDecodeBuffer, readposition, pixelcount);
		
		if (read != pixelcount) {
			PERROR("Could not read in all peak data, pixelcount is %d, read count is %d", pixelcount, read);
		}
		
#if defined (profile)
		int processtime = (int) (get_microseconds() - starttime);
		printf("Process time: %d useconds\n\n", processtime);
#endif

		if (read == 0) {
			return NO_PEAKDATA_FOUND;
		}
		
		*buffer = data->peakdataDecodeBuffer->destination[0];

		return read;
		
	// Micro view mode
	} else {
		// Calculate the amount of frames to be read
		nframes_t toRead = pixelcount * zoomStep[zoomLevel];
		
		TimeRef startlocation(startPos, 44100);
		nframes_t readFrames = m_source->file_read(data->peakdataDecodeBuffer, startlocation, toRead);

		if (readFrames == 0) {
			return NO_PEAKDATA_FOUND;
		}
		
		if ( readFrames != toRead) {
			PWARN("Unable to read nframes %d (only %d available)", toRead, readFrames);
			pixelcount = readFrames / zoomStep[zoomLevel];
		}

		int count = 0;
		nframes_t pos = 0;
		audio_sample_t valueMax, valueMin, sample;
		
		// MicroView needs a buffer to store the calculated peakdata
		// our decodebuffer's readbuffer is large enough for this purpose
		// and it's no problem to use it at this point in the process chain.
		float* peakdata = data->peakdataDecodeBuffer->readBuffer;

		do {
			valueMax = valueMin = 0;

			for(int i=0; i < zoomStep[zoomLevel]; i++) {
				Q_ASSERT(pos <= readFrames);
				sample = data->peakdataDecodeBuffer->destination[chan][pos];
				if (sample > valueMax)
					valueMax = sample;
				if (sample < valueMin)
					valueMin = sample;
				pos++;
			}

			if (valueMax > fabs(valueMin)) {
				peakdata[count] = valueMax;
			} else {
				peakdata[count] = valueMin;
			}
			
			count++;
		
		} while(count < pixelcount);


#if defined (profile)
		int processtime = (int) (get_microseconds() - starttime);
		printf("Process time: %d useconds\n\n", processtime);
#endif
		
		// Assign the supplied buffer to the 'real' peakdata buffer.
		*buffer = peakdata;
		
		return count;
	}

	
	return 1;
}


int Peak::prepare_processing()
{
	PENTER;
	
	foreach(ChannelData* data, m_channelData) {
		
		data->normFileName = data->fileName;
		data->normFileName.append(".norm");
		
		// Create read/write enabled file
		data->file = fopen(data->fileName.toUtf8().data(),"wb+");
		
		if (! data->file) {
			PWARN("Couldn't open peak file for writing! (%s)", data->fileName.toAscii().data());
			permanentFailure  = true;
			return -1;
		}
		
		// Create the temporary normalization data file
		data->normFile = fopen(data->normFileName.toUtf8().data(), "wb+");
		
		if (! data->normFile) {
			PWARN("Couldn't open normalization data file for writing! (%s)", data->normFileName.toAscii().data());
			permanentFailure  = true;
			return -1;
		}
		
		// We need to know the peakDataOffset.
		data->headerdata.peakDataOffset = 
					sizeof(data->headerdata.label) + 
					sizeof(data->headerdata.version) + 
					sizeof(data->headerdata.peakDataLevelOffsets) + 
					sizeof(data->headerdata.peakDataSizeForLevel) +
					sizeof(data->headerdata.normValuesDataOffset) + 
					sizeof(data->headerdata.peakDataOffset);
					
		// Now seek to the start position, so we can write the peakdata to it in the process function
		fseek(data->file, data->headerdata.peakDataOffset, SEEK_SET);
		
		data->pd = new Peak::ProcessData;
		data->pd->stepSize = TimeRef(1, m_source->get_file_rate());
	}
	
	
	return 1;
}


int Peak::finish_processing()
{
	PENTER;
	
	foreach(ChannelData* data, m_channelData) {
		
		if (data->pd->processLocation < data->pd->nextDataPointLocation) {
			peak_data_t peakvalue = (peak_data_t)(data->pd->peakUpperValue * MAX_DB_VALUE);
			fwrite(&peakvalue, sizeof(peak_data_t), 1, data->file);
			peakvalue = (peak_data_t)(-1 * data->pd->peakLowerValue * MAX_DB_VALUE);
			fwrite(&peakvalue, sizeof(peak_data_t), 1, data->file);
			data->pd->processBufferSize += 2;
		}
		
		int totalBufferSize = 0;
		
		data->headerdata.peakDataSizeForLevel[0] = data->pd->processBufferSize;
		totalBufferSize += data->pd->processBufferSize;
		
		for( int i = SAVING_ZOOM_FACTOR + 1; i < ZOOM_LEVELS+1; ++i) {
			data->headerdata.peakDataSizeForLevel[i - SAVING_ZOOM_FACTOR] = data->headerdata.peakDataSizeForLevel[i - SAVING_ZOOM_FACTOR - 1] / 2;
			totalBufferSize += data->headerdata.peakDataSizeForLevel[i - SAVING_ZOOM_FACTOR];
		}
		
		
		fseek(data->file, data->headerdata.peakDataOffset, SEEK_SET);
		
		// The routine below uses a different total buffer size calculation
		// which might end up with a size >= totalbufferSize !!!
		// Need to look into that, for now + 2 seems to work...
		peak_data_t* saveBuffer = new peak_data_t[totalBufferSize + 1*sizeof(peak_data_t)];
		
		int read = fread(saveBuffer, sizeof(peak_data_t), data->pd->processBufferSize, data->file);
		
		if (read != data->pd->processBufferSize) {
			PERROR("couldn't read in all saved data?? (%d read)", read);
		}
		
		
		int prevLevelBufferPos = 0;
		int nextLevelBufferPos;
		data->headerdata.peakDataSizeForLevel[0] = data->pd->processBufferSize;
		data->headerdata.peakDataLevelOffsets[0] = data->headerdata.peakDataOffset;
		
		for (int i = SAVING_ZOOM_FACTOR+1; i < ZOOM_LEVELS+1; ++i) {
		
			int prevLevelSize = data->headerdata.peakDataSizeForLevel[i - SAVING_ZOOM_FACTOR - 1];
			data->headerdata.peakDataLevelOffsets[i - SAVING_ZOOM_FACTOR] = data->headerdata.peakDataLevelOffsets[i - SAVING_ZOOM_FACTOR - 1] + prevLevelSize;
			prevLevelBufferPos = data->headerdata.peakDataLevelOffsets[i - SAVING_ZOOM_FACTOR - 1] - data->headerdata.peakDataOffset;
			nextLevelBufferPos = data->headerdata.peakDataLevelOffsets[i - SAVING_ZOOM_FACTOR] - data->headerdata.peakDataOffset;
			
			
			int count = 0;
			
			do {
				Q_ASSERT(nextLevelBufferPos <= totalBufferSize);
				saveBuffer[nextLevelBufferPos] = (peak_data_t) f_max(saveBuffer[prevLevelBufferPos], saveBuffer[prevLevelBufferPos + 2]);
				saveBuffer[nextLevelBufferPos + 1] = (peak_data_t) f_max(saveBuffer[prevLevelBufferPos + 1], saveBuffer[prevLevelBufferPos + 3]);
				nextLevelBufferPos += 2;
				prevLevelBufferPos += 4;
				count+=4;
			}
			while (count < prevLevelSize);
		}
		
		fseek(data->file, data->headerdata.peakDataOffset, SEEK_SET);
		
		int written = fwrite(saveBuffer, sizeof(peak_data_t), totalBufferSize, data->file);
		
		if (written != totalBufferSize) {
			PERROR("could not write complete buffer! (only %d)", written);
	// 		return -1;
		}
		
		fseek(data->normFile, 0, SEEK_SET);
		
		read = fread(saveBuffer, sizeof(audio_sample_t), data->pd->normDataCount, data->normFile);
		
		if (read != data->pd->normDataCount) {
			PERROR("Could not read in all (%d) norm. data, only %d", data->pd->normDataCount, read);
		}
		
		data->headerdata.normValuesDataOffset = data->headerdata.peakDataOffset + totalBufferSize;
		
		fclose(data->normFile);
		data->normFile = NULL;
		
		if (!QFile::remove(data->normFileName)) {
			PERROR("Failed to remove temp. norm. data file! (%s)", data->normFileName.toAscii().data()); 
		}
		
		written = fwrite(saveBuffer, sizeof(audio_sample_t), read, data->file);
		
		write_header(data);
		
		fclose(data->file);
		data->file = 0;
		
		delete [] saveBuffer;
		delete data->pd;
		data->pd = 0;
		
	}
	
	emit finished();
	
	return 1;
	
}


void Peak::process(uint channel, audio_sample_t* buffer, nframes_t nframes)
{
	ChannelData* data = m_channelData.at(channel);
	ProcessData* pd = data->pd;

	for (uint i=0; i < nframes; i++) {
		
		pd->processLocation += pd->stepSize;
		
		audio_sample_t sample = buffer[i];
		
		pd->normValue = f_max(pd->normValue, fabsf(sample));
		
		if (sample > pd->peakUpperValue) {
			pd->peakUpperValue = sample;
		}
		
		if (sample < pd->peakLowerValue) {
			pd->peakLowerValue = sample;
		}
		
		if (pd->processLocation >= pd->nextDataPointLocation) {
		
			peak_data_t peakbuffer[2];

			peakbuffer[0] = (peak_data_t) (pd->peakUpperValue * MAX_DB_VALUE );
			peakbuffer[1] = (peak_data_t) ((-1) * (pd->peakLowerValue * MAX_DB_VALUE ));
			
			int written = fwrite(peakbuffer, sizeof(peak_data_t), 2, data->file);
			
			if (written != 2) {
				PWARN("couldnt write data, only (%d)", written);
			}

			pd->peakUpperValue = 0.0;
			pd->peakLowerValue = 0.0;
			
			pd->processBufferSize+=2;
			pd->nextDataPointLocation += pd->processRange;
		}
		
		if (pd->normProcessedFrames == NORMALIZE_CHUNK_SIZE) {
			int written = fwrite(&pd->normValue, sizeof(audio_sample_t), 1, data->normFile);
			
			if (written != 1) {
				PWARN("couldnt write data, only (%d)", written);
			}
 
			pd->normValue = 0.0;
			pd->normProcessedFrames = 0;
			pd->normDataCount++;
		}
		
		pd->normProcessedFrames++;
	}
}


int Peak::create_from_scratch()
{
	PENTER;
	
#define profile

#if defined (profile)
	trav_time_t starttime = get_microseconds();
#endif
	int ret = -1;
	
	if (prepare_processing() < 0) {
		return ret;
	}
	
	m_source->set_output_rate(m_source->get_file_rate());
	
	nframes_t readFrames = 0;
	nframes_t totalReadFrames = 0;

	nframes_t bufferSize = 65536;

	int progression = 0;

	if (m_source->get_length() == 0) {
		qWarning("Peak::create_from_scratch() : m_source (%s) has length 0", m_source->get_name().toAscii().data());
		return ret;
	}

	if (m_source->get_nframes() < bufferSize) {
		bufferSize = 64;
		if (m_source->get_nframes() < bufferSize) {
			qDebug("source length is too short to display one pixel of the audio wave form in macro view");
			return ret;
		}
	}

	DecodeBuffer decodebuffer;
	
	do {
		if (interuptPeakBuild) {
			ret = -1;
			goto out;
		}
		
		readFrames = m_source->file_read(&decodebuffer, totalReadFrames, bufferSize);
		
		if (readFrames <= 0) {
			PERROR("readFrames < 0 during peak building");
			break;
		}
		
		for (uint chan = 0; chan < m_source->get_channel_count(); ++ chan) {
			process(chan, decodebuffer.destination[chan], readFrames);
		}
		
		totalReadFrames += readFrames;
		progression = (int) ((float)totalReadFrames / ((float)m_source->get_nframes() / 100.0));
		
		ChannelData* data = m_channelData.at(0);
		
		if ( progression > data->pd->progress) {
			emit progress(progression);
			data->pd->progress = progression;
		}
		
	} while (totalReadFrames < m_source->get_nframes());


	if (finish_processing() < 0) {
		ret = -1;
		goto out;
	}
	
	ret = 1;
	
out:
	 
#if defined (profile)
	long processtime = (long) (get_microseconds() - starttime);
	printf("Process time: %d seconds\n\n", (int)(processtime/1000));
#endif
	
	m_source->set_output_rate(44100);

	return ret;
}


audio_sample_t Peak::get_max_amplitude(nframes_t startframe, nframes_t endframe)
{
	foreach(ChannelData* data, m_channelData) {
		if (!data->file || !peaksAvailable) {
			return 0.0f;
		}
	}
	
	int startpos = startframe / NORMALIZE_CHUNK_SIZE;
	uint count = (endframe / NORMALIZE_CHUNK_SIZE) - startpos;
	
	uint buffersize = count < NORMALIZE_CHUNK_SIZE*2 ? NORMALIZE_CHUNK_SIZE*2 : count;
	audio_sample_t* readbuffer =  new audio_sample_t[buffersize];
	
	audio_sample_t maxamp = 0;
	
	// Read in the part not fully occupied by a cached normalize value
	// at the left hand part and run compute_peak on it.
	if (startframe != 0) {
		startpos += 1;
		int toRead = (int) ((startpos * NORMALIZE_CHUNK_SIZE) - startframe);
		
		int read = m_source->file_read(m_peakdataDecodeBuffer, startframe, toRead);
		
		for (uint chan = 0; chan < m_source->get_channel_count(); ++ chan) {
			maxamp = Mixer::compute_peak(m_peakdataDecodeBuffer->destination[chan], read, maxamp);
		}
	}
	
	
	// Read in the part not fully occupied by a cached normalize value
	// at the right hand part and run compute_peak on it.
	float f = (float) endframe / NORMALIZE_CHUNK_SIZE;
	int endpos = (int) f;
	int toRead = (int) ((f - (endframe / NORMALIZE_CHUNK_SIZE)) * NORMALIZE_CHUNK_SIZE);
	
	int read = m_source->file_read(m_peakdataDecodeBuffer, endframe - toRead, toRead);
	
	for (uint chan = 0; chan < m_source->get_channel_count(); ++ chan) {
		maxamp = Mixer::compute_peak(m_peakdataDecodeBuffer->destination[chan], read, maxamp);
	}
	
	// Now that we have covered both boundary situations,
	// read in the cached normvalues, and calculate the highest value!
	count = endpos - startpos;
	
	foreach(ChannelData* data, m_channelData) {
		fseek(data->file, data->headerdata.normValuesDataOffset + (startpos * sizeof(audio_sample_t)), SEEK_SET);
	
		read = fread(readbuffer, sizeof(audio_sample_t), count, data->file);
	
		if (read != (int)count) {
			printf("could only read %d, %d requested\n", read, count);
		}
	
		maxamp = Mixer::compute_peak(readbuffer, read, maxamp);
	}
	
	delete [] readbuffer;
	
	return maxamp;
}




/******** PEAK BUILD THREAD CLASS **********/
/******************************************/

PeakProcessor& pp()
{
	static PeakProcessor processor;
	return processor;
}


PeakProcessor::PeakProcessor()
{
	m_ppthread = new PPThread(this);
	m_taskRunning = false;
	m_runningPeak = 0;

	moveToThread(m_ppthread);
	
	m_ppthread->start();
	
	connect(this, SIGNAL(newTask()), this, SLOT(start_task()), Qt::QueuedConnection);
}


PeakProcessor::~ PeakProcessor()
{
	m_ppthread->exit(0);
	
	if (!m_ppthread->wait(1000)) {
		m_ppthread->terminate();
	}
	
	delete m_ppthread;
}


void PeakProcessor::start_task()
{
	m_runningPeak->create_from_scratch();
	
	QMutexLocker locker(&m_mutex);
	
	m_taskRunning = false;
	
	if (m_runningPeak->interuptPeakBuild) {
		PMESG("PeakProcessor:: Deleting interrupted Peak!");
		delete m_runningPeak;
		m_runningPeak = 0;
		m_wait.wakeAll();
		return;
	}
	
	foreach(Peak* peak, m_queue) {
		if (m_runningPeak->m_source->get_filename() == peak->m_source->get_filename()) {
			m_queue.removeAll(peak);
			emit peak->finished();
		}
	}
	
	m_runningPeak = 0;
	
	dequeue_queue();
}

void PeakProcessor::queue_task(Peak * peak)
{
	QMutexLocker locker(&m_mutex);
	
	m_queue.enqueue(peak);
	
	if (!m_taskRunning) {
		dequeue_queue();
	}
}

void PeakProcessor::dequeue_queue()
{
	if (!m_queue.isEmpty()) {
		m_taskRunning = true;
		m_runningPeak = m_queue.dequeue();
		emit newTask();
	}
}

void PeakProcessor::free_peak(Peak * peak)
{
	m_mutex.lock();
	
	m_queue.removeAll(peak);
	
	if (peak == m_runningPeak) {
		PMESG("PeakProcessor:: Interrupting running build process!");
		peak->interuptPeakBuild =  true;
		
		PMESG("PeakProcessor:: Waiting GUI thread until interrupt finished");
		m_wait.wait(&m_mutex);
		PMESG("PeakProcessor:: Resuming GUI thread");
		
		dequeue_queue();
		
		m_mutex.unlock();
		
		return;
	}
	
	m_mutex.unlock();
	
	delete peak;
}


PPThread::PPThread(PeakProcessor * pp)
{
	m_pp = pp;
}

void PPThread::run()
{
	exec();
}

