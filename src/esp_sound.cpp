#include "esp_sound.h"

static QueueHandle_t clipQueueHandle;

DMA_ATTR static signed short i2sBuffer[I2S_BUFFER_SIZE];

static void *_malloc(size_t size, const char* FILE, const int LINE)
{
    // check if SPIRAM is enabled and allocate on SPIRAM if allocatable
#if (CONFIG_SPIRAM_SUPPORT && (CONFIG_SPIRAM_USE_CAPS_ALLOC || CONFIG_SPIRAM_USE_MALLOC))
	auto before = ESP.getFreePsram();
	void* block = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
	log_i("PS RAM: %d/%d less %d as %08x-%08x => %d/%d at [%s:%d]", before, ESP.getPsramSize(), size, block, (uint8_t*)block + size-1, ESP.getFreePsram(), ESP.getPsramSize(), FILE, LINE);
	if (block) return block;
	throw std::out_of_range(StringF("[%s:%d] Failed to alloc %d", FILE, LINE, size).c_str());
#else
	throw std::out_of_range(StringF("[%s:%d] No PS RAM", FILE, LINE, size).c_str());
#endif
}

static void _free(void* block, const char* FILE, const int LINE) {
	log_i("Free %x at [%s %d]", block, FILE, LINE);
	std::free(block);
}

Clip::Clip () :
	maxSize(Sound::_clipSize),
	length(0),
  buffer(0) {
	//log_i("Constructing %08x", this);
}
Clip::~Clip() {
  if (buffer)
  	delete[] buffer;
}

Clip::Clip(const Clip& that) noexcept {
  //log_i("In copy constructor for %08x from %08x", this, &that);

  this->buffer = new int16_t[that.length];
  std::copy(that.buffer, that.buffer + that.length - 1, this->buffer);
  this->length = that.length;
  this->maxSize = that.maxSize;
}

Clip& Clip::operator=(const Clip& that) noexcept { // Move assignment operator 
  if (this->buffer) {
    //log_i("Deleting");
    delete[] this->buffer;
  }

  std::copy(that.buffer, that.buffer + that.length - 1, this->buffer);
  this->length = that.length;
  this->maxSize = that.maxSize;
  return *this;
}
Clip::Clip(Clip&& that) noexcept {
  //log_i("In move constructor for %08x from %08x", this, &that);

  this->buffer = that.buffer;
  that.buffer = 0;
  this->length = that.length;
  that.length = 0;
  this->maxSize = that.maxSize;
}

Clip& Clip::operator=(Clip&& that) noexcept { // Move assignment operator 
  if (this->buffer) {
    //log_i("Deleting");
    delete[] this->buffer;
  }

  this->buffer = that.buffer;
  that.buffer = 0;
  this->length = that.length;
  that.length = 0;
  this->maxSize = that.maxSize;
  return *this;
}

void Clip::appendSamples(int16_t* source, size_t count) {
  if (count == 0) throw std::logic_error(StringF("[%s:%d] count == 0", __FILE__, __LINE__).c_str());
  // Allow buffer to be expanded to cope with long Clips e.g. read from a file
	if (length == 0) {
    if (count > maxSize) maxSize = count;
  }
	if (count > maxSize - length) throw std::logic_error(StringF("[%s:%d] count %d but space left %d", __FILE__, __LINE__, count, maxSize - length).c_str());
  if (!buffer) {
    buffer = new int16_t[maxSize];
  }
	int16_t* buffPtr = buffer + length;
	memcpy((void*)buffPtr, (void*)source, count * 2);
  length += count;
}

void Clip::loadSamples(File& file, size_t count) {
  if (!buffer) {
    maxSize = count;
    buffer = new int16_t[maxSize];
  }
  size_t bytesRead = file.readBytes((char*)buffer, count * 2);
  length = bytesRead / 2;
  if (count != length) {
    throw std::logic_error(StringF("[%s:%d] count %d but read %d", __FILE__, __LINE__, count, length).c_str());
  }
}

void Sound::init(uint32_t samplingRate, size_t clipSize, size_t reserveClipCount) {
	// Start listening for audio: MONO @ 44.1KHz
  i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM),
      .sample_rate = samplingRate,
      .bits_per_sample = (i2s_bits_per_sample_t)BITS_PER_SAMPLE,
      .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = 0,
      .dma_buf_count = 8,
      .dma_buf_len = 512,
      .use_apll = false,
      .tx_desc_auto_clear = false,
      .fixed_mclk = -1,
  };
  
  i2s_pin_config_t pin_config = {
      // Setup for Xiao ESP32S3 Sense
      .bck_io_num = -1,    // IIS_SCLK was 26
      .ws_io_num = 42,     // IIS_LCLK was 32
      .data_out_num = -1,  // IIS_DSIN 
      .data_in_num = 41,   // IIS_DOUT was 33
  };
  esp_err_t ret = 0;

  if (i2s_driver_install((i2s_port_t)0, &i2s_config, 0, NULL) != ESP_OK) {
	  throw std::logic_error(StringF("[%s:%d] error in i2s_driver_install", __FILE__, __LINE__).c_str());
  }

  if (i2s_set_pin((i2s_port_t)0, &pin_config) != ESP_OK) {
	  throw std::logic_error(StringF("[%s:%d] error in i2s_set_pin", __FILE__, __LINE__).c_str());
  }

  if (i2s_zero_dma_buffer((i2s_port_t)0) != ESP_OK) {
	  throw std::logic_error(StringF("[%s:%d] error in i2s_zero_dma_buffer", __FILE__, __LINE__).c_str());
  }
  Sound::_samplingRate = samplingRate;
  Sound::_clipSize = clipSize;
  Sound::_reserveClipCount = reserveClipCount;
}

Sound::Sound() {
  // If the vector doesn't have reserved space it gets copied and extended by one every time .append() is called
  _clips.reserve(Sound::_reserveClipCount);
}

Sound::~Sound() {
}

bool Sound::getMicClip(Clip*& clip) {
  bool ret = xQueueReceive(clipQueueHandle, &clip, 0);
  if (ret) log_i("Received Clip %08x", clip);
  return ret;
}

void Sound::append(Clip& clip) {
  //log_i("In append(Clip& clip) for %08x", &clip);
  if (clip.length == 0) 
    throw std::logic_error(StringF("[%s:%d] clip is empty", __FILE__, __LINE__).c_str());
  
  //log_i("copy append(Clip&) for %08x", &clip);
  _clips.push_back(clip);  
}
void Sound::append(Clip&& clip) {
  //log_i("In append(Clip&& clip) for %08x", &clip);
  if (clip.length == 0) 
    throw std::logic_error(StringF("[%s:%d] clip is empty", __FILE__, __LINE__).c_str());
  
  //log_i("move append(Clip&&) for %08x", &clip);
  _clips.push_back(std::move(clip));
}
void Sound::append(Sound& that) {
  if (that._clips.size() == 0) 
    throw std::logic_error(StringF("[%s:%d] sound is empty", __FILE__, __LINE__).c_str());
  
  //log_i("copy append(Sound&) for %08x", &sound);
  for(const Clip& clip : that._clips) {
    _clips.push_back(clip);
  }
}
void Sound::append(Sound&& that) {
if (that._clips.size() == 0) 
    throw std::logic_error(StringF("[%s:%d] sound is empty", __FILE__, __LINE__).c_str());
  
  for(const Clip& clip : that._clips) {
    _clips.push_back(std::move(clip));
  }
}

float Sound::secs() {
  float secs = 0;
  for (const Clip& clip : _clips) {
    secs += ((float)clip.length) / Sound::_samplingRate;
  }
  return secs;
}

size_t Sound::totalSamples() {
  size_t sampleCount = 0;
  for (const Clip& clip : _clips) {
    sampleCount += ((float)clip.length);
  }
  return sampleCount;
}

void Sound::clear() {
  _clips.clear();
}

Sound& Sound::fromFile(FS& fs, const char* format, ...) {
  va_list args;
  va_start(args, format);
  return fromFile(fs, StringF(format, args));
}

Sound& Sound::fromFile(FS& fs, const String& filename) {
  _fromFilename = filename;
  _fs = &fs;
  return *this;
}

void Sound::load(missing_file_on_load_t missingFileOption) {
  if (_fromFilename.length() == 0) {
    throw std::invalid_argument(StringF("[%s:%d] Missing fromFilename() clause", __FILE__, __LINE__).c_str());
  }
  size_t bytesRead = 0;
  
  if (!_fs->exists(_fromFilename)) {
    if (missingFileOption == IGNORE_MISSING_FILE) {
      log_e("Missing file %s", _fromFilename.c_str());
      clear();
      return;
    } else {
      throw std::invalid_argument(StringF("[%s:%d] Missing file %s", __FILE__, __LINE__, _fromFilename.c_str()).c_str());
    }
  } 
  File file = _fs->open(_fromFilename, FILE_READ);

  size_t fileSize = file.size();

  size_t sampleCount = (fileSize - WAV_HEADER_SIZE) / 2;
  size_t fullClipCount = sampleCount / Sound::_clipSize;
  size_t lastSampleSize = sampleCount % Sound::_clipSize;

  if (file.readBytes(_header, WAV_HEADER_SIZE) != WAV_HEADER_SIZE
    ||
    strncmp(_header, "RIFF", 4) != 0) {
    throw std::invalid_argument(StringF("[%s:%d] Failed to read header from %s", __FILE__, __LINE__, _fromFilename.c_str()).c_str());
    // TBD Check that sampling rate is correct in header etc
  }

  for(int c = 0; c < fullClipCount; c++) {
    Clip* clip = new Clip();
    clip->loadSamples(file, _clipSize);
    _clips.push_back(std::move(*clip));
  }
  if (lastSampleSize > 0) {
    Clip* clip = new Clip();
    clip->loadSamples(file, lastSampleSize);
    _clips.push_back(std::move(*clip));
  }
  _fs = 0;
  _fromFilename = "";
}

Sound& Sound::toFile(FS& fs, const char* format, ...) {
  va_list args;
  va_start(args, format);
  return toFile(fs, StringF(format, args));
}

Sound& Sound::toFile(FS& fs, const String& filename) {
  _fs = &fs;
  _toFilename = filename;
	if (!_toFilename.startsWith("/")) {
		_toFilename = StringF("/%s", _toFilename);
  }
	if (_toFilename.indexOf("//") >= 0) {
		_toFilename.replace("//", "/");
	}

  return *this;
}

void Sound::save(existing_file_on_save_t existingFileOption) {
  // See this for reference: http://soundfile.sapp.org/doc/WaveFormat/
  size_t sampleCount = totalSamples();
  size_t wavContentSize = sampleCount * 2;
  uint32_t chunkSize = wavContentSize + WAV_HEADER_SIZE - 8;
  uint32_t byteRate = _samplingRate * BITS_PER_SAMPLE / 8;
  const uint8_t wavHeader[] = {
    'R', 'I', 'F', 'F',                                                   // ChunkID
    (uint8_t)chunkSize, (uint8_t)(chunkSize >> 8), (uint8_t)(chunkSize >> 16), (uint8_t)(chunkSize >> 24),      // ChunkSize
    'W', 'A', 'V', 'E',                                                   // Format
    'f', 'm', 't', ' ',                                                   // Subchunk1ID
    0x10, 0x00, 0x00, 0x00,                                               // Subchunk1Size (16 for PCM)
    0x01, 0x00,                                                           // AudioFormat (1 for PCM)
    0x01, 0x00,                                                           // NumChannels (1 channel)
    (uint8_t)_samplingRate, (uint8_t)(_samplingRate >> 8), (uint8_t)(_samplingRate >> 16), (uint8_t)(_samplingRate >> 24),  // SamplingRate
    (uint8_t)byteRate, (uint8_t)(byteRate >> 8), (uint8_t)(byteRate >> 16), (uint8_t)(byteRate >> 24),          // ByteRate
    0x02, 0x00,                                                           // BlockAlign
    0x10, 0x00,                                                           // BitsPerSample (16 bits)
    'd', 'a', 't', 'a',                                                   // Subchunk2ID
    (uint8_t)wavContentSize, (uint8_t)(wavContentSize >> 8), (uint8_t)(wavContentSize >> 16), (uint8_t)(wavContentSize >> 24),              // Subchunk2Size
  };
  memcpy(_header, wavHeader, sizeof(wavHeader));

  if (_fs->exists(_toFilename)) {
    if (existingFileOption == OVERWRITE_EXISTING_FILE) {
      log_i("File %s already exists, overwriting", _toFilename.c_str());
    } else {
      throw std::logic_error(StringF("[%s:%d] File %s already exists", __FILE__, __LINE__).c_str());
    }
  } else {
    // Check subdirs are in place and create if not
    char* subdirPath = strdup(_toFilename.c_str());
    char* slash = strchr(subdirPath + 1, '/');
    while (slash > 0) {
      *slash = '\0';
      if (!_fs->exists(subdirPath)) {
        _fs->mkdir(subdirPath);
        *slash = '/';
        slash = strchr(subdirPath + 1, '/');
      }
    }
    free(subdirPath);
  }
  File file = _fs->open(_toFilename, FILE_WRITE);

  if (!file) {
    throw std::logic_error(StringF("[%s:%d] Unable to create %s", __FILE__, __LINE__, _toFilename).c_str());
  }
  // Write header
  if (file.write((uint8_t*)_header, WAV_HEADER_SIZE) != WAV_HEADER_SIZE) {
    throw std::logic_error(StringF("[%s:%d] Unable to write header for %s", __FILE__, __LINE__, _toFilename).c_str());
  }
  // Write body of file
  for(const Clip& clip : _clips) {
    if (file.write((uint8_t*)clip.buffer, clip.length * 2) != clip.length * 2) {
      throw std::logic_error(StringF("[%s:%d] Unable to write to %s", __FILE__, __LINE__, _toFilename).c_str());
    }
  }
  file.close();
}

void TaskCaptureI2S(void* arg) {

  // Create a queue of clip handles for processing by the client app
  if (clipQueueHandle == 0) {
	  clipQueueHandle = xQueueCreate(3, sizeof(Clip*));
  }
  Clip* clip;
  const int32_t i2sBytesToRead = I2S_BUFFER_SIZE;
  size_t bytesRead;
  //log_i("Sound::_micOn = %s", Sound::_micOn ? "true" : "false");
  while (Sound::_micOn) {

    /* read data from i2s */
    i2s_read((i2s_port_t)0, (void*)i2sBuffer, i2sBytesToRead, &bytesRead, 100);

	  int samplesRead = bytesRead / 2;
    if (samplesRead <= 0) {
      log_e("Error in I2S read : %d", bytesRead);
    } else {
      if (bytesRead < i2sBytesToRead) {
        log_i("Partial I2S read");
      }
      //log_i("Read %d", bytesRead);
      // scale the data (otherwise the sound is too quiet)
      for (int x = 0; x < samplesRead; x++) {
        i2sBuffer[x] = i2sBuffer[x] << Sound::micGain;
      }

      // Copy from DMA RAM to a Clip object (in slower PS_RAM)
      if (! clip) {
        clip = new Clip();
        //log_i("First Clip is %08x", clip);
      }
      int spaceLeft = clip->maxSize - clip->length;
      int samplesToCopy = (spaceLeft < samplesRead) ? spaceLeft : samplesRead;
      //log_i("Appending %d", samplesToCopy);
      clip->appendSamples(i2sBuffer, samplesToCopy);
      int samplesCopied = samplesToCopy;
      if (clip->isFull()) {
        // Dispatch the current clip and start a new one
        log_i("Queuing Clip %08x", clip);
        xQueueSend(clipQueueHandle, (void*)&clip, 100); // If queue isn't being read and is full we lose the clip, tough.
        clip = new Clip();
        //log_i("Next Clip is %08x", clip);
        // Copy the remainder of the I2Sbuffer if any
        if (samplesCopied < samplesRead) {
          clip->appendSamples(i2sBuffer + spaceLeft, samplesRead - samplesCopied); 
        }
      }
      Sound::_stackHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
    }
    vTaskDelay(1);
  }
  vTaskDelete(NULL);
}

void Sound::startMic() {

  Sound::_micOn = true;
  xTaskCreate(TaskCaptureI2S, "Capture I2S", 4096, 0, 10, NULL);
}

void Sound::stopMic() {
  Sound::_micOn = false;
}

bool Sound::_micOn = false;
int Sound::micGain = 3;
int Sound::_stackHighWaterMark = 0;
uint32_t Sound::_samplingRate = SAMPLING_RATE;
size_t Sound::_clipSize = CLIP_SIZE;
size_t Sound::_reserveClipCount = RESERVE_CLIP_COUNT;