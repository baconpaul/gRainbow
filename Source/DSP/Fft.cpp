/*
  ==============================================================================

    Fft.cpp
    Created: 26 Apr 2021 9:01:42pm
    Author:  brady

  ==============================================================================
*/

#include "Fft.h"

Fft::Fft(int windowSize, int hopSize, double startProgress, double endProgress)
    : mWindowSize(windowSize),
      mHopSize(hopSize),
      mStartProgress(startProgress),
      mEndProgress(endProgress),
      mDiffProgress(mEndProgress - mStartProgress),
      mForwardFFT(std::log2(windowSize)),
      mWindowEnvelope(windowSize, juce::dsp::WindowingFunction<float>::WindowingMethod::blackmanHarris),
      juce::Thread("fft thread") {}

Fft::~Fft() {}

// Once a buffer is loaded, will run to produce mFftData and then will async
// notify when done
void Fft::run() {
  if (mInputBuffer == nullptr) return;
  clear(true);
  // Runs with first channel
  const int numInputSamples = mInputBuffer->getNumSamples();
  const float* pBuffer = mInputBuffer->getReadPointer(0);
  mFftFrame.resize(mWindowSize * 2, 0.0f);
  int curSample = 0;
  bool hasData = numInputSamples > mFftFrame.size();
  float curMax = std::numeric_limits<float>::min();

  while (hasData && !threadShouldExit()) {
    updateProgress(mStartProgress + (mDiffProgress * (static_cast<double>(curSample) / static_cast<double>(numInputSamples))));
    const float* startSample = &pBuffer[curSample];
    int numSamples = mFftFrame.size();
    if (curSample + mFftFrame.size() > numInputSamples) {
      numSamples = (numInputSamples - curSample);
    }
    mFftFrame.clear();
    mFftFrame.resize(mWindowSize * 2, 0.0f);
    memcpy(mFftFrame.data(), startSample, numSamples);
    mWindowEnvelope.multiplyWithWindowingTable(mFftFrame.data(), mFftFrame.size());

    // then render our FFT data..
    mForwardFFT.performFrequencyOnlyForwardTransform(mFftFrame.data());

    // Add fft data to our master array
    std::vector<float> newFrame = std::vector<float>(mFftFrame.begin(), mFftFrame.begin() + (mWindowSize / 2));
    float frameMax = juce::FloatVectorOperations::findMaximum(mFftFrame.data(), mFftFrame.size());
    if (frameMax > curMax) curMax = frameMax;
    mFftData.push_back(newFrame);
    // Normalize fft values according to max frame value
    for (size_t i = 0; i < mFftData.back().size(); ++i) {
      mFftData.back()[i] /= curMax;
    }

    curSample += mHopSize;
    if (curSample > numInputSamples) {
      hasData = false;
    }
  }

  if (onProcessingComplete != nullptr) {
    onProcessingComplete(mFftData);
  }
}

void Fft::clear(bool clearData) {
  mFftFrame.clear();
  if (clearData) {
    // The FFT can take up a lot of memory, need to not just clear, but have STD deallocate it
    mFftData.clear();
    mFftData.shrink_to_fit();
  }
}

void Fft::updateProgress(double progress) {
  if (onProgressUpdated != nullptr) {
    onProgressUpdated(progress);
  }
}

void Fft::process(const juce::AudioBuffer<float>* audioBuffer) {
  stopThread(4000);
  mInputBuffer = audioBuffer;
  startThread();
}