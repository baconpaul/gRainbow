/*
  ==============================================================================

    PitchDetector.cpp
    Created: 24 Apr 2021 2:07:10pm
    Author:  brady

  ==============================================================================
*/

#define _USE_MATH_DEFINES

#include "PitchDetector.h"
#include <limits.h>

PitchDetector::PitchDetector(double startProgress, double endProgress)
    : mStartProgress(endProgress / 2.0),
      mEndProgress(endProgress),
      mDiffProgress(mEndProgress - mStartProgress),
      mFft(FFT_SIZE, HOP_SIZE, startProgress, endProgress / 2.0),
      juce::Thread("pitch detector thread") {
  initHarmonicWeights();
  mFft.onProcessingComplete = [this](Utils::SpecBuffer& spectrum) {
    // Runs FFT twice but using custom size suited for the PitchDetector
    stopThread(4000);
    startThread();
  };
  mFft.onProgressUpdated = [this](float progress) { updateProgress(progress); };
}

PitchDetector::~PitchDetector() { stopThread(4000); }

void PitchDetector::process(const juce::AudioBuffer<float>* audioBuffer, double sampleRate) {
  cancelProcessing();
  updateProgress(mStartProgress);
  mSampleRate = sampleRate;
  mFft.process(audioBuffer);
}

void PitchDetector::cancelProcessing() {
  mFft.stopThread(4000);
  stopThread(4000);
}

void PitchDetector::run() {
  if (!computeHPCP()) return;
  if (onHarmonicProfileReady != nullptr) onHarmonicProfileReady(mHPCP);
  if (!segmentPitches()) return;
  getSegmentedPitchBuffer();
  updateProgress(mEndProgress);
  if (onPitchesReady != nullptr) onPitchesReady(mPitchMap, mSegmentedPitches);
}

void PitchDetector::clear() {
  mFft.clear(true);
  mPitchMap.clear();
}

void PitchDetector::updateProgress(double progress) {
  if (onProgressUpdated != nullptr) {
    onProgressUpdated(progress);
  }
}

void PitchDetector::getSegmentedPitchBuffer() {
  mSegmentedPitches.clear();
  for (int frame = 0; frame < mHPCP.size(); ++frame) {
    mSegmentedPitches.push_back(std::vector<float>(mHPCP[frame].size(), 0.0f));
  }
  for (Utils::PitchClass i : Utils::ALL_PITCH_CLASS) {
    std::vector<Pitch>& pitchVec = mPitchMap.getReference(i);
    for (int j = 0; j < pitchVec.size(); ++j) {
      auto pitch = pitchVec[j];
      auto duration = pitch.duration * mHPCP.size();
      int frame = pitch.posRatio * (mHPCP.size() - 1);
      int bin = (int)(pitch.pitchClass * (NUM_HPCP_BINS / 12.0));
      for (int j = 0; j < duration; ++j) {
        mSegmentedPitches[frame + j][bin] = pitch.gain;
      }
    }
  }
}

bool PitchDetector::computeHPCP() {
  mHPCP.clear();

  const Utils::SpecBuffer& spec = mFft.getSpectrum();
  for (size_t frame = 0; frame < spec.size(); ++frame) {
    if (threadShouldExit()) return false;
    updateProgress(mStartProgress + (mDiffProgress * (static_cast<double>(frame) / static_cast<double>(spec.size()))));
    mHPCP.push_back(std::vector<float>(NUM_HPCP_BINS, 0.0f));

    const std::vector<float>& specFrame = spec[frame];

    // Find local peaks to compute HPCP with
    std::vector<PitchDetector::Peak> peaks = getPeaks(MAX_SPEC_PEAKS, specFrame);

    float curMax = 0.0;
    for (int i = 0; i < peaks.size(); ++i) {
      if (threadShouldExit()) return false;
      float peakFreq = ((peaks[i].binNum / (specFrame.size() - 1)) * mSampleRate) / 2;
      if (peakFreq < MIN_FREQ || peakFreq > MAX_FREQ) continue;

      // Create sum for each pitch class
      for (int pc = 0; pc < NUM_HPCP_BINS; ++pc) {
        int pcIdx = (pc + PITCH_CLASS_OFFSET_BINS) % NUM_HPCP_BINS;
        float centerFreq = REF_FREQ * std::pow(2.0f, pc / (float)NUM_HPCP_BINS);

        // Add contribution from each harmonic
        for (int hIdx = 0; hIdx < mHarmonicWeights.size(); ++hIdx) {
          float freq = peakFreq * pow(2., -mHarmonicWeights[hIdx].semitone / 12.0);
          float harmonicWeight = mHarmonicWeights[hIdx].gain;
          float d = std::fmod(12.0f * std::log2(freq / centerFreq), 12.0f);
          if (std::abs(d) <= (0.5f * HPCP_WINDOW_LEN)) {
            float w = std::pow(std::cos((M_PI * d) / HPCP_WINDOW_LEN), 2.0f);
            mHPCP[frame][pcIdx] += (w * std::pow(peaks[i].gain, 2) * harmonicWeight * harmonicWeight);
            if (mHPCP[frame][pcIdx] > curMax) curMax = mHPCP[frame][pcIdx];
          }
        }
      }
    }

    // Normalize HPCP frame and clear low energy frames
    float totalEnergy = 0.0f;
    if (curMax > 0.0f) {
      for (int pc = 0; pc < NUM_HPCP_BINS; ++pc) {
        totalEnergy += mHPCP[frame][pc];
        mHPCP[frame][pc] /= curMax;
      }
    }
    if (totalEnergy / NUM_HPCP_BINS < MIN_AVG_FRAME_ENERGY) {
      std::fill(mHPCP[frame].begin(), mHPCP[frame].end(), 0.0f);
    }
  }
  return true;
}

bool PitchDetector::segmentPitches() {
  if (mHPCP.empty()) return false;

  mPitchMap.clear();
  for (int i = 0; i < mSegments.size(); ++i) {
    mSegments[i].isAvailable = true;
  }

  // Initialize parameters
  int maxIdleFrames = mSampleRate * (MAX_IDLE_TIME_MS / 1000.0) / HOP_SIZE;
  int minNoteFrames = mSampleRate * (MIN_NOTE_TIME_MS / 1000.0) / HOP_SIZE;
  float maxConfidence = 0;

  // Calculate note trajectories through the clip
  for (int frame = 0; frame < mHPCP.size(); ++frame) {
    if (threadShouldExit()) return false;
    // Get the new pitch candidates
    std::vector<PitchDetector::Peak> peaks = getPeaks(NUM_ACTIVE_SEGMENTS, mHPCP[frame]);

    // Look for continuation candidates in peaks
    for (int i = 0; i < mSegments.size(); ++i) {
      if (!mSegments[i].isAvailable) {
        int closestIdx = -1;
        for (int j = 0; j < peaks.size(); ++j) {
          float devBins = std::abs(mSegments[i].binNum - peaks[j].binNum);
          if (devBins <= MAX_DEVIATION_BINS) {
            // Replace candidate if:
            if (closestIdx == -1) {  // It is the first one
              closestIdx = j;
            } else if (devBins < std::abs(mSegments[i].binNum - peaks[closestIdx].binNum) ||  // It is closer to the target
                       (peaks[closestIdx].binNum == peaks[j].binNum &&
                        (peaks[closestIdx].gain < peaks[j].gain))) {  // It's tied for distance
                                                                      // but has a higher gain
              closestIdx = j;
            }
          }
        }
        if (closestIdx == -1) {
          // Mark segment as waiting for continuance
          if (mSegments[i].idleFrame == -1) mSegments[i].idleFrame = frame;
        } else {
          // Continue segment
          mSegments[i].idleFrame = -1;
          // Change bin num to better candidate if needed
          if (!hasBetterCandidateAhead(frame + 1, mSegments[i].binNum, std::abs(mSegments[i].binNum - peaks[closestIdx].binNum))) {
            mSegments[i].binNum = peaks[closestIdx].binNum;
          }
          mSegments[i].salience += peaks[closestIdx].gain;
          peaks[closestIdx].binNum = INVALID_BIN;  // Mark peak so it isn't reused for multiple
                                                   // segments
        }

        // Check for segment expiration
        if (mSegments[i].idleFrame > 0 && (frame - mSegments[i].idleFrame) > maxIdleFrames) {
          Utils::PitchClass pc = getPitchClass(mSegments[i].binNum);
          if (frame - mSegments[i].startFrame > minNoteFrames) {
            // Push to completed segments
            float confidence = mSegments[i].salience / (frame - mSegments[i].startFrame);
            if (confidence > maxConfidence) maxConfidence = confidence;
            mPitchMap.getReference(pc).push_back(Pitch(pc, (float)mSegments[i].startFrame / mHPCP.size(),
                                                      (float)(frame - mSegments[i].startFrame) / mHPCP.size(), confidence));
          }
          // Replace segment with new peak
          mSegments[i].isAvailable = true;
        }
      } else {
        // Replace segment with new peak
        for (int j = 0; j < peaks.size(); ++j) {
          if (peaks[j].binNum != INVALID_BIN) {
            mSegments[i].startFrame = frame;
            mSegments[i].idleFrame = -1;
            mSegments[i].binNum = peaks[j].binNum;
            mSegments[i].salience = peaks[j].gain;
            mSegments[i].isAvailable = false;
            break;
          }
        }
      }
    }
  }

  // Normalize pitch saliences
  for (Utils::PitchClass i : Utils::ALL_PITCH_CLASS) {
    std::vector<Pitch>& pitchVec = mPitchMap.getReference(i);
    for (int j = 0; j < pitchVec.size(); ++j) {
      pitchVec[j].gain /= maxConfidence;
    }
    // Sort pitches from high to low salience
    std::sort(pitchVec.begin(), pitchVec.end(), [](Pitch self, Pitch other) { return self.gain > other.gain; });
  }
  return true;
}

bool PitchDetector::hasBetterCandidateAhead(int startFrame, float target, float deviation) {
  int numLookaheadFrames = mSampleRate * (LOOKAHEAD_TIME_MS / 1000.0) / HOP_SIZE;
  for (int i = startFrame; i < startFrame + numLookaheadFrames; ++i) {
    if (i > mHPCP.size() - 1) return false;
    std::vector<PitchDetector::Peak> peaks = getPeaks(NUM_ACTIVE_SEGMENTS, mHPCP[i]);
    for (int j = 0; j < peaks.size(); ++j) {
      float peakDev = std::abs(target - peaks[j].binNum);
      if (peakDev < deviation) return true;
    }
  }
  return false;
}

Utils::PitchClass PitchDetector::getPitchClass(float binNum) {
  int binsPerClass = NUM_HPCP_BINS / 12;
  int pc = (int)(binNum / binsPerClass) % 12;
  return (Utils::PitchClass)pc;
}

PitchDetector::Peak PitchDetector::interpolatePeak(int frame, int bin) {
  // Use quadratic interpolation to find peak freq and amplitude
  const Utils::SpecBuffer& spec = mFft.getSpectrum();
  if (bin == 0 || bin == spec[frame].size() - 1) {
    return Peak((bin * mSampleRate) / FFT_SIZE, spec[frame][bin]);
  }
  float a = 20 * std::log10(spec[frame][bin - 1]);
  float b = 20 * std::log10(spec[frame][bin]);
  float c = 20 * std::log10(spec[frame][bin + 1]);

  float p = 0.5f * (a - c) / (a - (2.0f * b) + c);
  float interpBin = bin + p;
  float freq = (interpBin * mSampleRate) / FFT_SIZE;
  float gainDB = b - (0.25 * (a - c) * p);
  return Peak(freq, juce::jlimit(0.0, 1.0, std::pow(10, gainDB / 20.0f)));
}

// From essentia:
// Builds a weighting table of harmonic contribution. Higher harmonics
// contribute less and the fundamental frequency has a full harmonic
// Strength of 1.0.
void PitchDetector::initHarmonicWeights() {
  mHarmonicWeights.clear();

  // Populate _harmonicPeaks with the semitonal positions of each of the
  // harmonics.
  for (int i = 0; i <= NUM_HARMONIC_WEIGHTS; i++) {
    float semitone = 12.0 * log2(i + 1.0);
    float octweight = std::max(1.0, (semitone / 12.0) * 0.5);

    // Get the semitone within the range
    // (0-HARMONIC_PRECISION, 12.0-HARMONIC_PRECISION]
    while (semitone >= 12.0 - HARMONIC_PRECISION) {
      semitone -= 12.0;
    }

    // Check to see if the semitone has already been added to weights
    std::vector<HarmonicWeight>::iterator it;
    for (it = mHarmonicWeights.begin(); it != mHarmonicWeights.end(); it++) {
      if ((*it).semitone > semitone - HARMONIC_PRECISION && (*it).semitone < semitone + HARMONIC_PRECISION) break;
    }

    if (it == mHarmonicWeights.end()) {
      // no harmonic peak found for this frequency; add it
      mHarmonicWeights.push_back(HarmonicWeight(semitone, (1.0 / octweight)));
    } else {
      // else, add the weight
      (*it).gain += (1.0 / octweight);
    }
  }
}

std::vector<PitchDetector::Peak> PitchDetector::getPeaks(int numPeaks, const std::vector<float>& frame) {
  int size = frame.size();
  const float scale = 1.0 / (float)(size - 1);

  std::vector<Peak> peaks;
  peaks.reserve(size);

  // we want to round up to the next integer instead of simple truncation,
  // otherwise the peak frequency at i can be lower than _minPos
  int i = 0;

  // first check the boundaries:
  if (i + 1 < size && frame[i] > frame[i + 1]) {
    if (frame[i] > MAGNITUDE_THRESHOLD) {
      peaks.push_back(Peak(i, frame[i]));
    }
  }

  while (true) {
    // going down
    while (i + 1 < size - 1 && frame[i] >= frame[i + 1]) {
      i++;
    }

    // now we're climbing
    while (i + 1 < size - 1 && frame[i] < frame[i + 1]) {
      i++;
    }

    // not anymore, go through the plateau
    int j = i;
    while (j + 1 < size - 1 && (frame[j] == frame[j + 1])) {
      j++;
    }

    // end of plateau, do we go up or down?
    if (j + 1 < size - 1 && frame[j + 1] < frame[j] && frame[j] > MAGNITUDE_THRESHOLD) {  // going down again
      float resultBin = 0.0;
      float resultVal = 0.0;

      if (j != i) {  // plateau peak between i and j
        resultBin = (i + j) * 0.5;
        resultVal = frame[i];
      } else {  // interpolate peak at i-1, i and i+1
        interpolatePeak(frame[j - 1], frame[j], frame[j + 1], j, resultVal, resultBin);
      }

      if (resultBin > size - 1) break;

      peaks.push_back(Peak(resultBin, resultVal));
    }

    // nothing found, start loop again
    i = j;

    if (i + 1 >= size - 1) {  // check the one just before the last position
      if (i == size - 2 && frame[i - 1] < frame[i] && frame[i + 1] < frame[i] && frame[i] > MAGNITUDE_THRESHOLD) {
        float resultBin = 0.0;
        float resultVal = 0.0;
        interpolatePeak(frame[i - 1], frame[i], frame[i + 1], j, resultVal, resultBin);
        peaks.push_back(Peak(resultBin, resultVal));
      }
      break;
    }
  }

  // check upper boundary here, so peaks are already sorted by position
  float pos = 1.0 / scale;
  if (size - 2 < pos && pos <= size - 1 && frame[size - 1] > frame[size - 2]) {
    if (frame[size - 1] > MAGNITUDE_THRESHOLD) {
      peaks.push_back(Peak((size - 1), frame[size - 1]));
    }
  }

  // we only want this many peaks
  int nWantedPeaks = juce::jmin(numPeaks, (int)peaks.size());
  std::sort(peaks.begin(), peaks.end(), [](Peak self, Peak other) { return self.gain > other.gain; });
  return std::vector<Peak>(peaks.begin(), peaks.begin() + nWantedPeaks);
}

std::vector<PitchDetector::Peak> PitchDetector::getWhitenedPeaks(int numPeaks, const std::vector<float>& frame) {
  std::vector<Peak> peaks = getWhitenedPeaks(numPeaks, frame);
  const int nPeaks = peaks.size();

  // If there are no magnitudes to whiten, do nothing
  if (nPeaks == 0) {
    return peaks;
  }

  // Convert input linear magnitudes to dB scale
  for (int i = 0; i < nPeaks; ++i) {
    peaks[i].gain = float(2.0) * Utils::lin2db(peaks[i].gain);
  }

  // get max peak
  float maxAmp = peaks.front().gain;
  float spectralRange = mSampleRate / 2.0f;

  // compute envelope
  std::vector<float> xPointsNoiseBPF;
  std::vector<float> yPointsNoiseBPF;

  float incr = BPF_RESOLUTION;
  int specSize = frame.size();
  // reserve some meaningful space, i.e. size of sepctrum
  xPointsNoiseBPF.reserve(specSize);
  yPointsNoiseBPF.reserve(specSize);
  for (float freq = 0.0; freq <= MAX_FREQ && freq <= spectralRange; freq += incr) {  //# magic numbers in the body of this for loop
    float bf = freq - std::max(50.0, freq * 0.34);                                   // 0.66
    float ef = freq + std::max(50.0, freq * 0.58);                                   // 1.58
    int b = int(bf / spectralRange * (specSize - 1.0) + 0.5);
    int e = int(ef / spectralRange * (specSize - 1.0) + 0.5);
    b = std::max(b, 0);
    b = std::min(specSize - 1, b);
    e = std::max(e, b + 1);
    e = std::min(specSize, e);
    float c = b / 2.0 + e / 2.0;
    float halfwindowlength = e - c;

    float n = 0.0;
    float wavg = 0.0;

    for (int i = b; i < e; ++i) {
      float weight = 1.0 - abs(float(i) - c) / halfwindowlength;
      weight *= weight;
      weight *= weight;
      float spectrumEnergyVal = frame[i] * frame[i];
      weight *= spectrumEnergyVal;
      wavg += spectrumEnergyVal * weight;
      n += weight;
    }
    if (n != 0.0) wavg /= n;

    // Add points to the BPFs
    xPointsNoiseBPF.push_back(freq);
    yPointsNoiseBPF.push_back(wavg);
  }

  yPointsNoiseBPF[yPointsNoiseBPF.size() - 1] = yPointsNoiseBPF[yPointsNoiseBPF.size() - 2];

  for (int i = 0; i < int(yPointsNoiseBPF.size()); ++i) {
    // don't optimise the sqrt as 0.5 outside lin2db as it fails for the case
    // 0, due to previously converted magnitudes to db
    yPointsNoiseBPF[i] = float(2.0) * Utils::lin2db(sqrt(yPointsNoiseBPF[i]));
  }

  Utils::BPF noiseBPF = Utils::BPF(xPointsNoiseBPF, yPointsNoiseBPF);

  // compute envelope and peak difference to it
  std::vector<Peak> whitePeaks = peaks;
  for (int i = 0; i < nPeaks; ++i) {  //# lots of magic values below
    float freq = ((peaks[i].binNum / (frame.size() - 1)) * mSampleRate) / 2;
    float amp = peaks[i].gain;

    if (freq > MAX_FREQ - incr) {
      // Keep current gain
      continue;  // This used to be a break-statement, but a break-statement
                 // would only work if the "frequencies" and "magnitudesdB"
                 // vectors were ordered by frequency
    }

    float ampEnv = noiseBPF(freq);
    if (amp < maxAmp - 40.0) whitePeaks[i].gain = (maxAmp - 40.0 - amp) / 2.0;
    if (amp > ampEnv)
      whitePeaks[i].gain = 0.0;
    else if (amp > ampEnv - 30.0)
      whitePeaks[i].gain = amp - ampEnv;
    else
      whitePeaks[i].gain = -200.0;
    whitePeaks[i].gain -= 20.0 * freq / 4000.0;
  }

  // Convert the whitened magnitudes back to linear scale
  for (int i = 0; i < nPeaks; ++i) {
    // dividing by 2 due to converting to db => sqrt(lin2db(A)) lin2db(A/2)
    whitePeaks[i].gain = Utils::db2lin(whitePeaks[i].gain / 2.0);
  }
  return whitePeaks;
}

/**
 * http://ccrma.stanford.edu/~jos/parshl/Peak_Detection_Steps_3.html
 *
 * Estimating the "true" maximum peak (frequency and magnitude) of the detected
 * local maximum using a parabolic curve-fitting. The idea is that the main-lobe
 * of spectrum of most analysis windows on a dB scale looks like a parabola and
 * therefore the maximum of a parabola fitted through a local maximum bin and
 * it's two neighboring bins will give a good approximation of the actual
 * frequency and magnitude of a sinusoid in the input signal.
 *
 * The parabola f(x) = a(x-n)^2 + b(x-n) + c can be completely described using 3
 * points; f(n-1) = A1, f(n) = A2 and f(n+1) = A3, where A1 = 20log10(|X(n-1)|),
 * A2 = 20log10(|X(n)|), A3 = 20log10(|X(n+1)|).
 *
 * Solving these equation yields: a = 1/2*A1 - A2 + 1/2*A3, b = 1/2*A3 - 1/2*A1
 * and c = A2.
 *
 * As the 3 bins are known to be a maxima, solving d/dx f(x) = 0, yields the
 * fractional bin position x of the estimated peak. Substituting delta_x for
 * (x-n) in this equation yields the fractional offset in bins from n where the
 * peak's maximum is.
 *
 * Solving this equation yields: delta_x = 1/2 * (A1 - A3)/(A1 - 2*A2 + A3).
 *
 * Computing f(n+delta_x) will estimate the peak's magnitude (in dB's):
 * f(n+delta_x) = A2 - 1/4*(A1-A3)*delta_x.
 */
void PitchDetector::interpolatePeak(const float leftVal, const float middleVal, const float rightVal, int currentBin,
                                    float& resultVal, float& resultBin) const {
  float delta_x = 0.5 * ((leftVal - rightVal) / (leftVal - 2 * middleVal + rightVal));
  resultBin = currentBin + delta_x;
  resultVal = middleVal - 0.25 * (leftVal - rightVal) * delta_x;
}