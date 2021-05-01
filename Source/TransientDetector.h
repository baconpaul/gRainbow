/*
  ==============================================================================

    TransientDetector.h
    Created: 21 Apr 2021 9:34:38pm
    Author:  brady
    Juce implementation of method from:
    A TRANSIENT DETECTION ALGORITHM FOR AUDIO USING ITERATIVE
    ANALYSIS OF STFT
    Thoshkahna, Nsabimana, Ramakrishnan
    https://ismir2011.ismir.net/papers/PS2-6.pdf

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

class TransientDetector : juce::Thread {
 public:
  TransientDetector();
  ~TransientDetector() {}

  typedef struct Transient {
    float posRatio;
    float confidence;
    Transient(float posRatio, float confidence)
        : posRatio(posRatio), confidence(confidence) {}
  } Transient;

  std::function<void(std::vector<Transient>&)> onTransientsUpdated = nullptr;

  void processBuffer(juce::AudioBuffer<float>* fileBuffer);

  void run() override;

 private:
  static constexpr auto FFT_ORDER = 9;
  static constexpr auto FFT_SIZE = 1 << FFT_ORDER;
  static constexpr auto PARAM_THRESHOLD = 2.5f;
  static constexpr auto PARAM_SPREAD = 3;
  static constexpr auto PARAM_ATTACK_LOCK = 10;

  juce::dsp::FFT mForwardFFT;
  juce::AudioBuffer<float>* mFileBuffer = nullptr;
  std::array<float, FFT_SIZE * 2> mFftFrame;
  std::vector<std::vector<float>> mFftData;  // FFT data normalized from 0.0-1.0
  std::array<float, PARAM_SPREAD>
      mEnergyBuffer;  // Spectral energy rolling buffer
  std::vector<Transient> mTransients;
  int mAttackFrames = PARAM_ATTACK_LOCK;

  void updateFft();
  void retrieveTransients();
  // Using current energy buffer and attack frame counter, determines if current
  // frame is a transient frame
  bool isTransient();
};