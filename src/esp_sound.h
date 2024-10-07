#ifndef ESP_SOUND_H
#define ESP_SOUND_H
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s.h"
#include <FS.h>
#include "string.h"
#include "StringF.h"
#include "vector"
#include "FS.h"

static const uint32_t I2S_BUFFER_SIZE = 2048;
static const uint32_t SAMPLING_RATE = 44100;
static const uint32_t CLIP_SIZE = 22050; // 0.5 secs per clip
static const uint32_t RESERVE_CLIP_COUNT = 40;

#define BITS_PER_SAMPLE 16
#define WAV_HEADER_SIZE 44

typedef enum {
    IGNORE_MISSING_FILE,
    THROW_IF_MISSING
} missing_file_on_load_t;

typedef enum {
    OVERWRITE_EXISTING_FILE,
    THROW_IF_EXISTS
} existing_file_on_save_t;

class Clip {
	public:
		Clip();
		~Clip();
		Clip(const Clip&) noexcept;  // Copy constructor
		Clip& operator=(const Clip&) noexcept; // Copy assignment operator
		Clip(Clip&&) noexcept;  // Move constructor
		Clip& operator=(Clip&&) noexcept; // Move assignment operator
		size_t maxSize;
		size_t length;
		int16_t* buffer;
		void appendSamples(int16_t* source, size_t count);
		void loadSamples(File& file, size_t count);
		bool isFull() { return length == maxSize; }
};

class Sound {
  friend void TaskCaptureI2S(void* args);
  friend Clip;
  public:
    Sound();
    ~Sound();
	static void init(uint32_t samplingRate, size_t clipSize, size_t reserveClipCount);
	static void startMic();
	static void stopMic();
	static bool getMicClip(Clip*& clip);
	Sound& fromFile(FS& fs, const char* format, ...);
	Sound& fromFile(FS& fs, const String& path);
	Sound& toFile(FS& fs, const char* format, ...);
	Sound& toFile(FS& fs, const String& path);
	void load(missing_file_on_load_t = IGNORE_MISSING_FILE);
	void append(Clip& c);  // Copy append
	void append(Clip&& c);  // Move append
	void append(Sound& s);  // Copy append
	void append(Sound&& s);  // Move append
	void save(existing_file_on_save_t = OVERWRITE_EXISTING_FILE);
	void clear();
	static int micGain;
  	static bool _micOn;
	static int _stackHighWaterMark;
	static uint32_t _samplingRate;
	static size_t _clipSize;
	static size_t _reserveClipCount;
	float secs();
	size_t totalSamples();
  private:
	std::vector<Clip> _clips;
	bool _from;
	bool _to;
	FS* _fs;
	String _fromFilename;
	String _toFilename;
	char _header[WAV_HEADER_SIZE];
};
#endif