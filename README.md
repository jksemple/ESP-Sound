# ESP-Sound
 
This library allows WAV sounds to be loaded from file, captured from mic and saved to file on ESP32 with PS_RAM

## Loading

Sounds can be loaded from file storage (e.g. SD card) or captured from an I2S mic.
Sounds can either be loaded to replace any previous content or appended on to the end of an existing Sound.

## Capturing

Sound samples can be copied to a new Sound object or appended onto an existing one

## Saving

Sound objects can be saved to file e.g. SD card

## Classes

The main class in this library is Sound which contains one or more Clips that are consecutive in time and are saved consecutively to WAV.

## Error handling

Errors arising from logic errors (such as trying to save an empty Sound or to combine two sounds of differing bitrates) cause a C++ exception to be thrown.
This makes error handling easier as the whole of an area of code can be wrapped in a single try/catch block which will catch any such errors in one place instead of needing to check a return value from every method call, which makes code less succinct.
When loading a sound from a file the developer has the option whether to either regard a missing file as an exception or to just load an empty sound and ignore it.
When saving a sound to a file similarly the developer can choose whether the presence of an existing file with the requested name is regarded as an exception or just to overwrite it.

Note that exceptions are costly to process so the developer should aim to structure their code such that exceptions are very rarely or never thrown once code is 'working'.

#### Examples
```cpp
Sound::init(44100, 22050, 20); // initialise sampling rate, clip length and typical clip count
Sound mySound1, mySound2, mySound3, mySound4;
mySound1.fromFile(SD, "/abc.jpg").load();
mySound2.append(mySound3);	// Copy mySound3
mySound2.append(std::move(mySound3)); // Consume mySound3's sample buffer
Sound::startMic(); // Start a separate listening thread 
Clip* myClip;
if (Sound::getMicClip(myClip)) {
  mySound4.append(std::move(*myClip));
}
Sound::stopMic();
mySound4.toFile("/def.jpg").save();
```
