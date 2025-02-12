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

#include <juce_core/juce_core.h>
#include "Fft.h"

class TransientDetector : juce::Thread {
 public:
  TransientDetector(double startProgress, double endProgress);
  ~TransientDetector();

  typedef struct Transient {
    float posRatio;
    float confidence;
    Transient(float posRatio, float confidence) : posRatio(posRatio), confidence(confidence) {}
  } Transient;

  std::function<void(std::vector<Transient>&)> onTransientsUpdated = nullptr;
  std::function<void(double progress)> onProgressUpdated = nullptr;

  void process(const juce::AudioBuffer<float>* audioBuffer);

  void run() override;

 private:
  static constexpr auto FFT_SIZE = 1024;
  static constexpr auto HOP_SIZE = 512;
  static constexpr auto PARAM_THRESHOLD = 2.5f;
  static constexpr auto PARAM_SPREAD = 3;
  static constexpr auto PARAM_ATTACK_LOCK = 10;

  // Used to show far along the run thread is
  void updateProgress(double progress);
  double mStartProgress;
  double mEndProgress;
  double mDiffProgress;

  Fft mFft;
  std::array<float, PARAM_SPREAD> mEnergyBuffer;  // Spectral energy rolling buffer
  std::vector<Transient> mTransients;
  int mAttackFrames = PARAM_ATTACK_LOCK;

  void updateFft();
  void retrieveTransients();
  // Using current energy buffer and attack frame counter, determines if current
  // frame is a transient frame
  bool isTransient();
};