/*
  ==============================================================================

    This file contains the basic startup code for a JUCE application.

  ==============================================================================
*/

#include <JuceHeader.h>
#include "FileAudioSource.h"
#include "ScalingAudioSource.h"

#define INPUT_ARG "--input|-i"
#define OUTPUT_ARG "--output|-o"
#define TIME_ARG "--time|-t"

constexpr static double maxRatio = 10;
constexpr static double minRatio = 0.1;
constexpr static int bufferSize = 4096;

//==============================================================================
int pvoc (int argc, char* argv[])
{
	ArgumentList argList = ArgumentList(argc, argv);
	File inputFile = argList.getExistingFileForOption(INPUT_ARG);
	File outputFile = argList.getFileForOption(OUTPUT_ARG);
	double timeRatio = 1.0;
	String timeArg = argList.getValueForOption(TIME_ARG);
	if (timeArg.isNotEmpty()) {
		timeRatio = timeArg.getDoubleValue();
	}
	if (timeRatio < minRatio || timeRatio > maxRatio) {
		String message("Invalid time ratio: ");
		ConsoleApplication::fail(message << timeArg);
	}
	
	AudioFormatManager formatManager{};
	formatManager.registerBasicFormats();
	String wildcards = formatManager.getWildcardForAllFormats();
	
	std::unique_ptr<AudioFormatReader> reader(formatManager.createReaderFor(inputFile));
	if (!reader) {
		String message("Invalid input file: ");
		ConsoleApplication::fail(message << inputFile.getFullPathName());
	}
	reader->input->setPosition(0);
	
	std::unique_ptr<AudioFormatWriter> writer;
	
	std::unique_ptr<FileOutputStream> outputStream = outputFile.createOutputStream();
	if (outputStream->failedToOpen()) {
		ConsoleApplication::fail(outputStream->getStatus().getErrorMessage());
	}
	outputStream->setPosition(0);
	outputStream->truncate();
	
	AudioFormat *format = formatManager.findFormatForFileExtension(outputFile.getFileExtension());
	if (!format) {
		String message("Unknown output format: ");
		ConsoleApplication::fail(message << outputFile.getFileExtension());
	}
	
	writer.reset(format->createWriterFor(
		// writer will delete stream
		outputStream.release(),
		reader->sampleRate,
		reader->numChannels,
		reader->bitsPerSample,
		reader->metadataValues,
		0
	));

	AudioSampleBuffer inputBuffer(reader->numChannels, bufferSize * maxRatio);
	AudioSampleBuffer outputBuffer(reader->numChannels, bufferSize);
    inputBuffer.clear();
    outputBuffer.clear();
    
	ltfat_pv_state_s* pv{nullptr};
	int status = ltfat_pv_init_s(maxRatio, reader->numChannels, 4096, &pv);
	if (status == LTFATERR_FAILED) {
		ConsoleApplication::fail("Failed to initialize phase vocoder");
	}
	
	float* outPtr[10];
	bool valid = true;
	int readerPos = 0;
	while (valid && !reader->input->isExhausted()) {
		int inLen = (int)ltfat_pv_nextinlen_s(pv, bufferSize);
		valid = reader->read(&inputBuffer, 0, inLen, readerPos, true, true);
		readerPos += inLen;
		auto inPtr = const_cast<const float**>( inputBuffer.getArrayOfReadPointers() );
		for(int ch=0; ch < reader->numChannels; ch++) {
			outPtr[ch] = outputBuffer.getWritePointer(ch, 0);
        }
		ltfat_pv_execute_s(pv, inPtr, inLen, reader->numChannels, timeRatio, bufferSize, outPtr);
		writer->writeFromAudioSampleBuffer(outputBuffer, 0, bufferSize);
	}
	writer->flush();
    return 0;
}

int main (int argc, char* argv[]) {
	return ConsoleApplication::invokeCatchingFailures([argc, argv] { return pvoc(argc, argv); });
}
