/*
  ==============================================================================

    Parameters.h
    Created: 10 Aug 2021 6:27:45pm
    Author:  brady

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

namespace ParamIDs {
// Generator params
static juce::String genEnable{"gen_enable_"};
static juce::String genSolo{"gen_solo_"};
static juce::String genWaiting{"gen_waiting_"};
static juce::String genCandidate{"gen_candidate_"};
static juce::String genPitchAdjust{"gen_pitch_adjust_"};
static juce::String genPositionAdjust{"gen_position_adjust_"};
static juce::String genGrainShape{"gen_grain_shape_"};
static juce::String genGrainTilt{"gen_grain_tilt_"};
static juce::String genGrainRate{"gen_grain_rate_"};
static juce::String genGrainDuration{"gen_grain_duration_"};
static juce::String genGrainGain{"gen_grain_gain_"};
static juce::String genAttack{"gen_attack_"};
static juce::String genDecay{"gen_decay_"};
static juce::String genSustain{"gen_sustain_"};
static juce::String genRelease{"gen_release_"};
// Position candidate params
static juce::String candidateValid{"candidate_valid_"};
static juce::String candidatePosRatio{"candidate_pos_ratio_"};
static juce::String candidatePbRate{"candidate_pb_rate_"};
static juce::String candidateDuration{"candidate_duration_"};
static juce::String candidateSalience{"candidate_salience_"};
// Global params
static juce::String globalAttack{"global_attack"};
static juce::String globalDecay{"global_decay"};
static juce::String globalSustain{"global_sustain"};
static juce::String globalRelease{"global_release"};
}  // namespace ParamIDs

struct ParamHelper {
  static juce::String getParamID(juce::AudioProcessorParameter* param) {
    if (auto paramWithID =
            dynamic_cast<juce::AudioProcessorParameterWithID*>(param))
      return paramWithID->paramID;

    return param->getName(50);
  }
};

static constexpr auto MAX_CANDIDATES = 6;
static constexpr auto NUM_NOTES = 12;
static constexpr auto NUM_GENERATORS = 4;
static constexpr auto ENV_LUT_SIZE = 128;

struct CandidateParams {
  CandidateParams(int noteIdx, int candidateIdx)
      : noteIdx(noteIdx), candidateIdx(candidateIdx) {}

  void addParams(juce::AudioProcessor& p);

  int noteIdx;
  int candidateIdx;
  juce::AudioParameterBool* valid = nullptr;
  juce::AudioParameterFloat* posRatio = nullptr;
  juce::AudioParameterFloat* pbRate   = nullptr;
  juce::AudioParameterFloat* duration = nullptr;
  juce::AudioParameterFloat* salience = nullptr;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CandidateParams)
};

struct GeneratorParams : juce::AudioProcessorParameter::Listener {
  GeneratorParams(int noteIdx, int genIdx) : noteIdx(noteIdx), genIdx(genIdx) {
    grainShape->addListener(this);
    grainTilt->addListener(this);
  }
  ~GeneratorParams() {
    grainShape->removeListener(this);
    grainTilt->removeListener(this);
  }

  void addParams(juce::AudioProcessor& p);
  void parameterValueChanged(int, float) override { updateGrainEnvelope(); };
  void parameterGestureChanged(int, bool) override {}
  void updateGrainEnvelope();

  int noteIdx;
  int genIdx;

  juce::AudioParameterBool* enable = nullptr;
  juce::AudioParameterBool* solo = nullptr;
  juce::AudioParameterBool* waiting = nullptr;
  juce::AudioParameterInt* candidate = nullptr;
  juce::AudioParameterFloat* pitchAdjust = nullptr;
  juce::AudioParameterFloat* positionAdjust = nullptr;
  juce::AudioParameterFloat* grainShape = nullptr;
  juce::AudioParameterFloat* grainTilt = nullptr;
  juce::AudioParameterFloat* grainRate = nullptr;
  juce::AudioParameterFloat* grainDuration = nullptr;
  juce::AudioParameterFloat* grainGain = nullptr;
  juce::AudioParameterFloat* attack = nullptr;
  juce::AudioParameterFloat* decay = nullptr;
  juce::AudioParameterFloat* sustain = nullptr;
  juce::AudioParameterFloat* release = nullptr;
  std::vector<float> grainEnv;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GeneratorParams)
};

struct NoteParam {
  NoteParam(int noteIdx) : noteIdx(noteIdx) {
    for (int i = 0; i < NUM_GENERATORS; ++i) {
      generators.emplace_back(new GeneratorParams(noteIdx, i));
    }
    for (int i = 0; i < MAX_CANDIDATES; ++i) {
      candidates.emplace_back(new CandidateParams(noteIdx, i));
    }
  }

  void addParams(juce::AudioProcessor& p);

  int noteIdx;
  std::vector<std::unique_ptr<GeneratorParams>> generators;
  std::vector<std::unique_ptr<CandidateParams>> candidates;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NoteParam)
};

struct NoteParams {
  NoteParams() {
    for (int i = 0; i < NUM_NOTES; ++i) {
      notes.emplace_back(new NoteParam(i));
    }
  }

  void addParams(juce::AudioProcessor& p);

  std::vector<std::unique_ptr<NoteParam>> notes;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NoteParams)
};

struct GlobalParams {
  GlobalParams() {}

  void addParams(juce::AudioProcessor& p);

  juce::AudioParameterFloat* attack = nullptr;
  juce::AudioParameterFloat* decay = nullptr;
  juce::AudioParameterFloat* sustain = nullptr;
  juce::AudioParameterFloat* release = nullptr;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GlobalParams)
};