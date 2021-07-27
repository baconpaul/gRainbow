/*
  ==============================================================================

    GranularSynth.cpp
    Created: 12 Apr 2021 11:22:41am
    Author:  brady

  ==============================================================================
*/

#include "GranularSynth.h"

GranularSynth::GranularSynth() : juce::Thread("granular thread") {
  mTotalSamps = 0;
  mGrains.ensureStorageAllocated(MAX_GRAINS);
  resetPositions();

  // Initialize grain triggering timestamps
  for (int i = 0; i < mGrainTriggersMs.size(); ++i) {
    float durMs = juce::jmap(mPositionSettings[i].duration, MIN_DURATION_MS,
                             MAX_DURATION_MS);
    mGrainTriggersMs[i] =
        juce::jmap(1.0f - mPositionSettings[i].rate, durMs / 8, durMs / 2);
  }

  startThread();
}

GranularSynth::~GranularSynth() { stopThread(4000); }

void GranularSynth::run() {
  while (!threadShouldExit()) {

    GranularSynth::PositionParams params =
        mPositionSettings[mNextPositionToPlay];
    float durMs = juce::jmap(params.duration,
                             MIN_DURATION_MS, MAX_DURATION_MS);

    if (mFileBuffer != nullptr) {
      // Add one grain per active note
      for (GrainNote& gNote : mActiveNotes) {
        // Skip if not enabled or full of grains
        if (mGrains.size() >= MAX_GRAINS) continue;
        if (!gNote.positions[mNextPositionToPlay].isActive) continue;

        juce::Random random;
        GrainPositionFinder::GrainPosition& gPos =
            gNote.positions[mNextPositionToPlay];
        float durSamples = mSampleRate * (durMs / 1000) * (1.0f / gPos.pbRate);
        float posSamples = gPos.pitch.posRatio * mFileBuffer->getNumSamples();
        float posOffset = juce::jmap(
            random.nextFloat(), 0.0f,
            (gPos.pitch.duration * mFileBuffer->getNumSamples()) - durSamples);

        float gain = gPos.ampEnvLevel * params.gain;
        jassert(gPos.pbRate > 0.1f);
        auto grain = Grain(generateGrainEnvelope(params.shape), durSamples, gPos.pbRate,
                           posSamples + posOffset, mTotalSamps, gain);
        mGrains.add(grain);

        // Update amplitude envelope for position that just played
        float attackSamples =
            mSampleRate *
            juce::jmap(params.attack, MIN_ATTACK_SEC, MAX_ATTACK_SEC);
        float decaySamples =
            mSampleRate *
            juce::jmap(params.decay, MIN_DECAY_SEC, MAX_DECAY_SEC);
        switch (gPos.envState) {
          case Utils::EnvelopeState::ATTACK: {
            gPos.ampEnvLevel = juce::jlimit(0.0f, 1.0f, (mTotalSamps - gNote.noteOnTs) / (float)attackSamples);
            if (gPos.ampEnvLevel >= 1.0f) {
              gPos.envState = Utils::EnvelopeState::DECAY;
            }
            break;
          }
          case Utils::EnvelopeState::DECAY: {
            gPos.ampEnvLevel = juce::jlimit(
                params.sustain, 1.0f,
                1.0f - ((mTotalSamps - attackSamples - gNote.noteOnTs) /
                        (float)(decaySamples)) *
                           (1.0f - params.sustain));
            if (gPos.ampEnvLevel <= params.sustain) {
              gPos.envState = Utils::EnvelopeState::SUSTAIN;
            }
            break;
          }
          case Utils::EnvelopeState::SUSTAIN: {
            gPos.ampEnvLevel = params.sustain;
            // Note: setting note off sets state to release, we don't need to
            // here
            break;
          }
          case Utils::EnvelopeState::RELEASE: {
            float releaseSamples =
                mSampleRate *
                juce::jmap(params.release, MIN_RELEASE_SEC, MAX_RELEASE_SEC);
            gPos.ampEnvLevel = juce::jlimit(
                0.0f, params.sustain,
                params.sustain * (1.0f - (mTotalSamps - gNote.noteOffTs) /
                                             (float)releaseSamples));
            break;
          }
        }
      }
    }

    // Get next position to play
    // TODO: reflect actual rate like below in envelops viz
    mGrainTriggersMs[mNextPositionToPlay] =
        juce::jmap(1.0f - mPositionSettings[mNextPositionToPlay].rate,
                   durMs * MIN_RATE_RATIO, durMs * MAX_RATE_RATIO);
    auto nextGrainPosition = mNextPositionToPlay;
    float nextGrainWaitTime = mGrainTriggersMs[mNextPositionToPlay];
    for (int i = 0; i < mGrainTriggersMs.size(); ++i) {
      if (mGrainTriggersMs[i] < nextGrainWaitTime) {
        nextGrainWaitTime = mGrainTriggersMs[i];
        nextGrainPosition = (Utils::PositionColour)i;
      }
    }

    mNextPositionToPlay = nextGrainPosition;

    // Subtract wait time from each trigger
    for (int i = 0; i < mGrainTriggersMs.size(); ++i) {
      mGrainTriggersMs[i] -= nextGrainWaitTime;
    }

    wait(juce::jmax(1.0f, nextGrainWaitTime));
  }
}

void GranularSynth::process(juce::AudioBuffer<float>& buffer) {
  // Add contributions from each grain
  juce::Array<Grain> grains = mGrains; // Copy to avoid threading issues
  for (int i = 0; i < buffer.getNumSamples(); ++i) {
    for (int g = 0; g < grains.size(); ++g) {
      grains[g].process(*mFileBuffer, buffer, mTotalSamps);
    }
    mTotalSamps++;
  }

  // Reset timestamps if no grains active to keep numbers low
  if (mActiveNotes.isEmpty() && mGrains.isEmpty()) {
    mTotalSamps = 0;
  } else {
    // Normalize the block before sending onward
    // if grains is empty, don't want to divide by zero
    for (int i = 0; i < buffer.getNumSamples(); ++i) {
      for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        //float* channelBlock = buffer->getWritePointer(ch);
        //channelBlock[i] /= mGrains.size();
      }
    }
  }

  // Delete expired grains
  mGrains.removeIf([this](Grain& g) { return mTotalSamps > (g.trigTs + g.duration); });

  // Delete expired notes
  long maxReleaseTime = getMaxReleaseTime();
  mActiveNotes.removeIf([this, maxReleaseTime](GrainNote& gNote) {
    return (mTotalSamps - gNote.noteOffTs) > maxReleaseTime && gNote.noteOffTs > 0;
  });
}

void GranularSynth::setFileBuffer(juce::AudioBuffer<float>* buffer, double sr) {
  mFileBuffer = buffer;
  mSampleRate = sr;
}

void GranularSynth::setPitches(
    juce::HashMap<Utils::PitchClass, std::vector<PitchDetector::Pitch>>*
        pitches) {
  mPositionFinder.setPitches(pitches);
}

void GranularSynth::updateCurPositions() {
  if (mCurPitchClass == Utils::PitchClass::NONE || mFileBuffer == nullptr) return;
  std::vector<GrainPositionFinder::GrainPosition> gPositions =
      mPositionFinder.findPositions(Utils::MAX_POSITIONS, mCurPitchClass);
  std::vector<GrainPositionFinder::GrainPosition> gPosToPlay;
  for (int i = 0; i < Utils::PositionColour::NUM_POS; ++i) {
    if (mPositionSettings[i].isActive &&
        (gPositions.size() - 1) >= mPositions[mCurPitchClass][i]) {
      gPositions[mPositions[mCurPitchClass][i]].isActive = true;
      gPosToPlay.push_back(gPositions[mPositions[mCurPitchClass][i]]);
    } else {
      // Push back an inactive position for the synth
      gPosToPlay.push_back(
          GrainPositionFinder::GrainPosition(PitchDetector::Pitch(), 1.0));
    }
  }
  mCurPositions = gPosToPlay;
}

std::vector<int> GranularSynth::getBoxPositions() {
  if (mCurPitchClass == Utils::PitchClass::NONE) return std::vector<int>();
  return std::vector<int>(mPositions[mCurPitchClass].begin(),
                          mPositions[mCurPitchClass].end());
}

int GranularSynth::incrementPosition(int boxNum, bool lookRight) {
  int pos = mPositions[mCurPitchClass][boxNum];
  for (int i = 1; i <= Utils::MAX_POSITIONS; ++i) {
    int newPos = lookRight ? pos + i : pos - i;
    newPos = newPos % Utils::MAX_POSITIONS;
    bool isValid = true;
    for (int j = 0; j < mPositions[mCurPitchClass].size(); ++j) {
      if (mPositions[mCurPitchClass][j] == newPos && boxNum != j) {
        // Position already taken, move on
        isValid = false;
        break;
      }
    }
    if (isValid) {
      pos = newPos;
      break;
    }
  }
  mPositions[mCurPitchClass][boxNum] = pos;
  return pos;
}

void GranularSynth::resetPositions() {
  mCurPositions.clear();
  for (int i = 0; i < mPositions.size(); ++i) {
    for (int j = 0; j < mPositions[i].size(); ++j) {
      mPositions[i][j] = j;
    }
  }
}

void GranularSynth::setNoteOn(Utils::PitchClass pitchClass) {
  mCurPitchClass = pitchClass;
  updateCurPositions();
  bool isPlayingAlready = false;
  for (GrainNote& gNote : mActiveNotes) {
    if (gNote.pitchClass == pitchClass) {
      isPlayingAlready = true;
      gNote.positions = mCurPositions;
      gNote.noteOnTs = mTotalSamps;
      gNote.noteOffTs = -1;
    }
  }
  if (!isPlayingAlready) {
    mActiveNotes.add(GrainNote(pitchClass, mCurPositions, mTotalSamps));
  }
}

void GranularSynth::setNoteOff(Utils::PitchClass pitchClass) {
  for (GrainNote& gNote : mActiveNotes) {
    if (gNote.pitchClass == pitchClass) {
      // Set note off timestamp and set env state to release for each pos
      gNote.noteOffTs = mTotalSamps;
      for (int i = 0; i < gNote.positions.size(); ++i) {
        gNote.positions[i].envState = Utils::EnvelopeState::RELEASE;
      }
      break;
    }
  }
}

void GranularSynth::updatePositionStates(
    std::vector<bool> positionStates) {
  for (int i = 0; i < positionStates.size(); ++i) {
    mPositionSettings[i].isActive = positionStates[i];
  }
}

void GranularSynth::updateParameters(Utils::PositionColour colour,
  PositionParams params) {
  mPositionSettings[colour] = params;
  // Initialize grain triggering timestamps
  for (int i = 0; i < mGrainTriggersMs.size(); ++i) {
    float durMs = juce::jmap(mPositionSettings[i].duration, MIN_DURATION_MS,
                             MAX_DURATION_MS);
    mGrainTriggersMs[i] =
        juce::jmap(1.0f - mPositionSettings[i].rate, durMs / 8, durMs / 2);
  }
}

void GranularSynth::updateParameter(Utils::PositionColour colour,
                                    ParameterType param, float value) {
  switch (param) {
    case ParameterType::SHAPE:
      mPositionSettings[colour].shape = value;
      break;
    case ParameterType::RATE:
      mPositionSettings[colour].rate = value;
      break;
    case ParameterType::DURATION:
      mPositionSettings[colour].duration = value;
      break;
    case ParameterType::GAIN:
      mPositionSettings[colour].gain = value;
      break;
    case ParameterType::ATTACK:
      mPositionSettings[colour].attack = value;
      break;
    case ParameterType::DECAY:
      mPositionSettings[colour].decay = value;
      break;
    case ParameterType::SUSTAIN:
      mPositionSettings[colour].sustain = value;
      break;
    case ParameterType::RELEASE:
      mPositionSettings[colour].release = value;
      break;
    default:
      break;
  }
}

std::vector<float> GranularSynth::generateGrainEnvelope(float shape) {
  std::vector<float> grainEnv;
  // Each half: f(x) = 3nx(1-x)^2 + 3nx^2(1-x) + x^3
  for (int i = 0; i < 512; i++) {
    float x = (float)i / 512;
    float t = 3.0f * shape * x * std::pow(1.0f - x, 2.0f) +
              3.0f * shape * std::pow(x, 2.0f) * (1.0f - x) + std::pow(x, 3.0f);
    if (t <= 0.5f) {
      t *= 2.0f;
    } else {
      t = (1.0f - t) * 2.0f;
    }
    grainEnv.push_back(3.0f * shape * t * std::pow(1.0f - t, 2.0f) +
                      3.0f * shape * std::pow(t, 2.0f) * (1.0f - t) +
                      std::pow(t, 3.0f));
  }
  return grainEnv;
}

long GranularSynth::getMaxReleaseTime() {
  int curMaxSamples = 0;
  for (int i = 0; i < mPositionSettings.size(); ++i) {
    int releaseSamples = mSampleRate * juce::jmap(mPositionSettings[i].release,
                                                  MIN_RELEASE_SEC,
                                                  MAX_RELEASE_SEC);
    if (releaseSamples > curMaxSamples) {
      curMaxSamples = releaseSamples;
    }
  }
  return curMaxSamples;
}