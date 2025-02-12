/*
  ==============================================================================

    GrainControl.cpp
    Created: 23 Jun 2021 8:34:54pm
    Author:  brady

  ==============================================================================
*/

#include "GrainControl.h"
#include "../Utils.h"

GrainControl::GrainControl(Parameters& parameters)
    : mParameters(parameters),
      mCurSelectedParams(parameters.selectedParams),
      mSliderPitchAdjust(parameters, ParamCommon::Type::PITCH_ADJUST),
      mSliderPitchSpray(parameters, ParamCommon::Type::PITCH_SPRAY),
      mSliderPosAdjust(parameters, ParamCommon::Type::POS_ADJUST),
      mSliderPosSpray(parameters, ParamCommon::Type::POS_SPRAY)
{

  juce::Colour colour = Utils::GLOBAL_COLOUR;

  // Adjust pitch
  mSliderPitchAdjust.setNumDecimalPlacesToDisplay(2);
  mSliderPitchAdjust.setRange(ParamRanges::PITCH_ADJUST.start, ParamRanges::PITCH_ADJUST.end, 0.01);
  addAndMakeVisible(mSliderPitchAdjust);

  mLabelPitchAdjust.setText("Pitch Adjust", juce::dontSendNotification);
  mLabelPitchAdjust.setColour(juce::Label::ColourIds::textColourId, colour);
  mLabelPitchAdjust.setJustificationType(juce::Justification::centredTop);
  addAndMakeVisible(mLabelPitchAdjust);

  // Pitch spray
  mSliderPitchSpray.setTextValueSuffix("cents");
  mSliderPitchSpray.setSliderStyle(juce::Slider::SliderStyle::LinearBar);
  mSliderPitchSpray.setNumDecimalPlacesToDisplay(3);
  mSliderPitchSpray.setColour(juce::Slider::ColourIds::textBoxOutlineColourId, colour);
  mSliderPitchSpray.setColour(juce::Slider::ColourIds::textBoxTextColourId, colour);
  mSliderPitchSpray.setColour(juce::Slider::ColourIds::trackColourId, juce::Colours::white);
  mSliderPitchSpray.setRange(ParamRanges::PITCH_SPRAY.start, ParamRanges::PITCH_SPRAY.end, 0.005);
  addAndMakeVisible(mSliderPitchSpray);

  mLabelPitchSpray.setText("Pitch Spray", juce::dontSendNotification);
  mLabelPitchSpray.setColour(juce::Label::ColourIds::textColourId, colour);
  mLabelPitchSpray.setJustificationType(juce::Justification::centredTop);
  addAndMakeVisible(mLabelPitchSpray);

  // Adjust position
  mSliderPosAdjust.setNumDecimalPlacesToDisplay(2);
  mSliderPosAdjust.setRange(ParamRanges::POSITION_ADJUST.start, ParamRanges::POSITION_ADJUST.end, 0.01);
  addAndMakeVisible(mSliderPosAdjust);

  mLabelPosAdjust.setText("Position Adjust", juce::dontSendNotification);
  mLabelPosAdjust.setColour(juce::Label::ColourIds::textColourId, colour);
  mLabelPosAdjust.setJustificationType(juce::Justification::centredTop);
  addAndMakeVisible(mLabelPosAdjust);

  // Position spray
  mSliderPosSpray.setTextValueSuffix("s");
  mSliderPosSpray.setSliderStyle(juce::Slider::SliderStyle::LinearBar);
  mSliderPosSpray.setNumDecimalPlacesToDisplay(3);
  mSliderPosSpray.setColour(juce::Slider::ColourIds::textBoxOutlineColourId, colour);
  mSliderPosSpray.setColour(juce::Slider::ColourIds::textBoxTextColourId, colour);
  mSliderPosSpray.setColour(juce::Slider::ColourIds::trackColourId, juce::Colours::white);
  mSliderPosSpray.setRange(ParamRanges::POSITION_SPRAY.start, ParamRanges::POSITION_SPRAY.end, 0.005);
  addAndMakeVisible(mSliderPosSpray);

  mLabelPosSpray.setText("Position Spray", juce::dontSendNotification);
  mLabelPosSpray.setColour(juce::Label::ColourIds::textColourId, colour);
  mLabelPosSpray.setJustificationType(juce::Justification::centredTop);
  addAndMakeVisible(mLabelPosSpray);

  mPositionChanger.onPositionChanged = [this](bool isRight) {
    ParamGenerator* gen = dynamic_cast<ParamGenerator*>(mParameters.selectedParams);
    jassert(gen != nullptr);
    int numCandidates = mParameters.note.notes[gen->noteIdx]->candidates.size();
    int pos = gen->candidate->get();
    if (numCandidates == 0) return pos;
    int newPos = isRight ? pos + 1 : pos - 1;
    newPos = (newPos + numCandidates) % numCandidates;
    ParamHelper::setParam(gen->candidate, newPos);
    mPositionChanger.setPositionNumber(newPos);
  };
  mPositionChanger.onSoloChanged = [this](bool isSolo) {
    if (mCurSelectedParams->type == ParamType::GENERATOR) {
      ParamGenerator* gen = dynamic_cast<ParamGenerator*>(mCurSelectedParams);
      ParamHelper::setParam(mParameters.note.notes[gen->noteIdx]->soloIdx, isSolo ? gen->genIdx : SOLO_NONE);
    }
  };
  mPositionChanger.setColour(colour);
  addAndMakeVisible(mPositionChanger);

  mCurSelectedParams->addListener(this);
  updateSelectedParams();

  startTimer(100);
}

void GrainControl::parameterValueChanged(int idx, float value) { mParamHasChanged.store(true); }

void GrainControl::timerCallback() {
  if (mParamHasChanged.load()) {
    mParamHasChanged.store(false);
    mSliderPitchAdjust.setValue(mParameters.getFloatParam(mCurSelectedParams, ParamCommon::Type::PITCH_ADJUST),
                                juce::dontSendNotification);
    mSliderPitchSpray.setValue(mParameters.getFloatParam(mCurSelectedParams, ParamCommon::Type::PITCH_SPRAY),
                               juce::dontSendNotification);
    mSliderPosAdjust.setValue(mParameters.getFloatParam(mCurSelectedParams, ParamCommon::Type::POS_ADJUST),
                              juce::dontSendNotification);
    mSliderPosSpray.setValue(mParameters.getFloatParam(mCurSelectedParams, ParamCommon::Type::POS_SPRAY),
                             juce::dontSendNotification);
    if (mCurSelectedParams->type == ParamType::GENERATOR) {
      ParamGenerator* gen = dynamic_cast<ParamGenerator*>(mCurSelectedParams);
      mPositionChanger.setPositionNumber(gen->candidate->get());
      mPositionChanger.setSolo(mParameters.note.notes[gen->noteIdx]->soloIdx->get() == gen->genIdx);
    } else {
      mPositionChanger.setSolo(false);
    }
  }
}

void GrainControl::updateSelectedParams() { 
  if (mCurSelectedParams != nullptr) mCurSelectedParams->removeListener(this);
  mCurSelectedParams = mParameters.selectedParams;
  mCurSelectedParams->addListener(this);

  mParamColour = mParameters.getSelectedParamColour();
  mSliderPitchAdjust.updateSelectedParams();
  mSliderPitchSpray.updateSelectedParams();
  mSliderPosAdjust.updateSelectedParams();
  mSliderPosSpray.updateSelectedParams();
  // TODO: disable position changer if not generator type
  bool isGen = mCurSelectedParams->type == ParamType::GENERATOR;
  mPositionChanger.setActive(isGen);
  if (isGen) {
    ParamGenerator* gen = dynamic_cast<ParamGenerator*>(mCurSelectedParams);
    mPositionChanger.setNumPositions(mParameters.note.notes[gen->noteIdx]->candidates.size());
    mPositionChanger.setPositionNumber(gen->candidate->get());
    mPositionChanger.setColour(mParamColour);
  }
  
  mParamHasChanged.store(true);
  repaint();
}

void GrainControl::paint(juce::Graphics& g) {
  juce::Colour colour = mParamColour;

  // Section title
  g.setColour(Utils::GLOBAL_COLOUR);
  g.fillRoundedRectangle(mTitleRect, Utils::ROUNDED_AMOUNT);
  g.setColour(colour);
  g.drawRoundedRectangle(mTitleRect, Utils::ROUNDED_AMOUNT, 2.0f);
  g.setColour(juce::Colours::white);
  g.drawText(juce::String(SECTION_TITLE), mTitleRect, juce::Justification::centred);

  /*float envWidth;
  float envOffset;
  if (mParameters.global.grainSync->get()) {
    float durDiv = std::pow(2, (int)(ParamRanges::SYNC_DIV_MAX * mParameters.global.grainDuration->get()));
    envWidth = mVizRect.getWidth() / durDiv;
    float rateDiv = std::pow(2, (int)(ParamRanges::SYNC_DIV_MAX * mParameters.global.grainRate->get()));
    envOffset = envWidth / rateDiv;
  } else {
    envWidth = juce::jmap(mParameters.global.grainDuration->get(), mVizRect.getWidth() / MAX_NUM_ENVS, mVizRect.getWidth());
    envOffset = juce::jmap(mParameters.global.grainRate->get(), envWidth * MIN_RATE_RATIO, envWidth * MAX_RATE_RATIO);
  }

  float shapeWidth = envWidth * mParameters.global.grainShape->get() / 2.0f;

  // Draw darker odd numbered envelopes
  float tilt = mParameters.global.grainTilt->get();
  float curXStart = mVizRect.getX() + envOffset;
  juce::Colour envColour = colour.darker(0.5f);
  while (curXStart < mVizRect.getWidth()) {
    juce::Path envPath;
    envPath.startNewSubPath(juce::Point<float>(curXStart, mVizRect.getBottom()));
    envPath.lineTo(juce::Point<float>(limitEnvX(juce::jmax(curXStart, curXStart + (tilt * envWidth) - shapeWidth)), mVizRect.getY()));
    envPath.lineTo(
        juce::Point<float>(limitEnvX(juce::jmin(curXStart + envWidth, curXStart + (tilt * envWidth) + shapeWidth)), mVizRect.getY()));
    envPath.lineTo(juce::Point<float>(limitEnvX(curXStart + envWidth), mVizRect.getBottom()));
    g.setColour(envColour);
    g.strokePath(envPath, mPathStroke);
    curXStart += (envOffset * 2.0f);
  }
  curXStart = mVizRect.getX();
  // Draw brighter even numbered envelopes
  envColour = colour.brighter(0.5f);
  while (curXStart < mVizRect.getWidth()) {
    juce::Path envPath;
    envPath.startNewSubPath(juce::Point<float>(curXStart, mVizRect.getBottom()));
    envPath.lineTo(juce::Point<float>(limitEnvX(juce::jmax(curXStart, curXStart + (tilt * envWidth) - shapeWidth)), mVizRect.getY()));
    envPath.lineTo(
        juce::Point<float>(limitEnvX(juce::jmin(curXStart + envWidth, curXStart + (tilt * envWidth) + shapeWidth)), mVizRect.getY()));
    envPath.lineTo(juce::Point<float>(limitEnvX(curXStart + envWidth), mVizRect.getBottom()));
    g.setColour(envColour);
    g.strokePath(envPath, mPathStroke);
    curXStart += (envOffset * 2.0f);
  }

  g.drawRect(mVizRect.expanded(2).withCentre(mVizRect.getCentre()), 2.0f); */
  g.setColour(mParamColour);
  g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), Utils::ROUNDED_AMOUNT, 2.0f);
}

void GrainControl::resized() {
  juce::Rectangle<int> r = getLocalBounds();
  // Remove padding
  r = r.reduced(Utils::PADDING, Utils::PADDING).withCentre(r.getCentre());

  // Make title rect
  mTitleRect = r.removeFromTop(Utils::TITLE_HEIGHT).toFloat();

  r.removeFromTop(Utils::PADDING);

  int panelWidth = r.getWidth() / 3;
  int panelHeight = Utils::LABEL_HEIGHT * 3 + Utils::KNOB_HEIGHT;

  juce::Rectangle<int> paramPanel = r.removeFromBottom(panelHeight);

  // Pitch components
  juce::Rectangle<int> pitchPanel = paramPanel.removeFromLeft(panelWidth);
  mLabelPitchSpray.setBounds(pitchPanel.removeFromBottom(Utils::LABEL_HEIGHT));
  mSliderPitchSpray.setBounds(pitchPanel.removeFromBottom(Utils::LABEL_HEIGHT));
  mLabelPitchAdjust.setBounds(pitchPanel.removeFromBottom(Utils::LABEL_HEIGHT));
  mSliderPitchAdjust.setBounds(
      pitchPanel.removeFromBottom(Utils::KNOB_HEIGHT).withSizeKeepingCentre(Utils::KNOB_HEIGHT * 2, Utils::KNOB_HEIGHT));

  // Position components
  juce::Rectangle<int> positionPanel = paramPanel.removeFromRight(panelWidth);
  mLabelPosSpray.setBounds(positionPanel.removeFromBottom(Utils::LABEL_HEIGHT));
  mSliderPosSpray.setBounds(positionPanel.removeFromBottom(Utils::LABEL_HEIGHT));
  mLabelPosAdjust.setBounds(positionPanel.removeFromBottom(Utils::LABEL_HEIGHT));
  mSliderPosAdjust.setBounds(
      positionPanel.removeFromBottom(Utils::KNOB_HEIGHT).withSizeKeepingCentre(Utils::KNOB_HEIGHT * 2, Utils::KNOB_HEIGHT));

  // Candidate changer
  mPositionChanger.setBounds(paramPanel.withSizeKeepingCentre(paramPanel.getWidth(), paramPanel.getHeight() / 2));

  // TODO: param viz rect
}
