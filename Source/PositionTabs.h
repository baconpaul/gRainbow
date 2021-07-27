/*
  ==============================================================================

    PositionTabs.h
    Created: 21 Jul 2021 6:22:07pm
    Author:  brady

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "Utils.h"

//==============================================================================
/*
 */
class PositionTabs : public juce::Component {
 public:
  PositionTabs();
  ~PositionTabs() override;

  void paint(juce::Graphics&) override;
  void resized() override;

  void mouseMove(const juce::MouseEvent& event) override;
  void mouseExit(const juce::MouseEvent& event) override;
  void mouseUp(const juce::MouseEvent& event) override;

  std::function<void(Utils::PositionColour tab, bool isSelected,
                     bool isEnabled)>
      onTabChanged = nullptr;


 private:
  static constexpr auto TOGGLE_SIZE = 16;

  /* UI Components */
  std::array<juce::ToggleButton, Utils::PositionColour::NUM_POS> mBtnsEnabled;

  /* Bookkeeping */
  Utils::PositionColour mCurSelectedTab = Utils::PositionColour::BLUE;
  int mCurHoverTab = -1;

  void tabChanged(Utils::PositionColour tab, bool isSelected, bool isEnabled);

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PositionTabs)
};
