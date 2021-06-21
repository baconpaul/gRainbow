#include "MainComponent.h"

//==============================================================================
MainComponent::MainComponent()
    : mKeyboard(mKeyboardState),
      mFft(FFT_SIZE, HOP_SIZE),
      juce::Thread("main fft thread"),
      mProgressBar(mLoadingProgress) {
  mFormatManager.registerBasicFormats();

  setLookAndFeel(&mRainbowLookAndFeel);

  juce::Image logo = juce::PNGImageFormat::loadFrom(juce::File(
      "C:/Users/brady/Documents/GitHub/gRainbow/gRainbow-circles.png"));

  /* Title section */
  // mLogo.setImage(logo, juce::RectanglePlacement::centred);
  // addAndMakeVisible(mLogo);

  mBtnOpenFile.setButtonText("Open File");
  mBtnOpenFile.onClick = [this] { openNewFile(); };
  addAndMakeVisible(mBtnOpenFile);

  mBtnRecord.setButtonText("Start Recording");
  mBtnRecord.setColour(juce::TextButton::ColourIds::buttonColourId,
                       juce::Colours::green);
  mBtnRecord.onClick = [this] {
    mIsRecording = !mIsRecording;
    if (mIsRecording) {
      mBtnRecord.setButtonText("Stop Recording");
      mBtnRecord.setColour(juce::TextButton::ColourIds::buttonColourId,
                           juce::Colours::red);
    } else {
      mBtnRecord.setButtonText("Start Recording");
      mBtnRecord.setColour(juce::TextButton::ColourIds::buttonColourId,
                           juce::Colours::green);
    }
  };
  addAndMakeVisible(mBtnRecord);

  /* -------------- Knobs --------------*/

  /* Diversity */
  mSliderDiversity.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
  mSliderDiversity.setSliderStyle(juce::Slider::SliderStyle::Rotary);
  mSliderDiversity.setRange(0.0, 1.0, 0.01);
  mSliderDiversity.onValueChange = [this] {
    mSynth.setDiversity(mSliderDiversity.getValue());
  };
  mSliderDiversity.setValue(PARAM_DIVERSITY_DEFAULT, juce::sendNotification);
  addAndMakeVisible(mSliderDiversity);

  mLabelDiversity.setText("Diversity", juce::dontSendNotification);
  mLabelDiversity.setJustificationType(juce::Justification::centredTop);
  addAndMakeVisible(mLabelDiversity);

  /* Rate */
  mSliderRate.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
  mSliderRate.setSliderStyle(juce::Slider::SliderStyle::Rotary);
  mSliderRate.setRange(0.0, 1.0, 0.01);
  mSliderRate.onValueChange = [this] {
    mSynth.setRate(mSliderRate.getValue());
  };
  mSliderRate.setValue(PARAM_RATE_DEFAULT, juce::sendNotification);
  addAndMakeVisible(mSliderRate);

  mLabelRate.setText("Rate", juce::dontSendNotification);
  mLabelRate.setJustificationType(juce::Justification::centredTop);
  addAndMakeVisible(mLabelRate);

  /* Duration */
  mSliderDuration.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
  mSliderDuration.setSliderStyle(juce::Slider::SliderStyle::Rotary);
  mSliderDuration.setRange(0.0, 1.0, 0.01);
  mSliderDuration.onValueChange = [this] {
    mSynth.setDuration(mSliderDuration.getValue());
  };
  mSliderDuration.setValue(PARAM_DURATION_DEFAULT, juce::sendNotification);
  addAndMakeVisible(mSliderDuration);

  mLabelDuration.setText("Duration", juce::dontSendNotification);
  mLabelDuration.setJustificationType(juce::Justification::centredTop);
  addAndMakeVisible(mLabelDuration);

  addAndMakeVisible(mArcSpec);
  mArcSpec.onPositionUpdated = [this](int midiNote,
                                      GrainPositionFinder::GrainPosition gPos) {
    mPositionFinder.updatePosition(midiNote, gPos);
  };
  
  addChildComponent(mProgressBar);

  mTransientDetector.onTransientsUpdated =
      [this](std::vector<TransientDetector::Transient>& transients) {
        mArcSpec.setTransients(&transients);
      };

  mPitchDetector.onPitchesUpdated =
      [this](std::vector<std::vector<float>>& hpcpBuffer, std::vector<std::vector<float>>& notesBuffer) {
        mArcSpec.loadBuffer(&hpcpBuffer, ArcSpectrogram::SpecType::HPCP);
        mArcSpec.loadBuffer(&notesBuffer, ArcSpectrogram::SpecType::NOTES);
        mPositionFinder.setPitches(&mPitchDetector.getPitches());
        mFileLoaded = true;
      };

  mPitchDetector.onProgressUpdated = [this](float progress) {
    mLoadingProgress = progress;
    juce::MessageManagerLock lock;
    if (progress >= 1.0) {
      mProgressBar.setVisible(false);
    } else if (!mProgressBar.isVisible()) {
      mProgressBar.setVisible(true);
    } 
  };

  addAndMakeVisible(mKeyboard);

  setSize(1200, 600);

  // Some platforms require permissions to open input channels so request that
  // here
  if (juce::RuntimePermissions::isRequired(
          juce::RuntimePermissions::recordAudio) &&
      !juce::RuntimePermissions::isGranted(
          juce::RuntimePermissions::recordAudio)) {
    juce::RuntimePermissions::request(
        juce::RuntimePermissions::recordAudio,
        [&](bool granted) { setAudioChannels(granted ? 2 : 0, 2); });
  } else {
    // Specify the number of input and output channels that we want to open
    setAudioChannels(2, 2);
  }

  startTimer(50);  // Keyboard focus timer
}

MainComponent::~MainComponent() {
  setLookAndFeel(nullptr);
  stopThread(4000);
  // This shuts down the audio device and clears the audio source.
  shutdownAudio();
}

void MainComponent::timerCallback() {
  if (mCurPitchClass != PitchDetector::PitchClass::NONE) {
    int k = juce::jmap((float)mSliderDiversity.getValue(), MIN_DIVERSITY,
                       MAX_DIVERSITY);
    std::vector<GrainPositionFinder::GrainPosition> gPositions =
        mPositionFinder.findPositions(k, mCurPitchClass);
    mSynth.setPositions(mCurPitchClass, gPositions);
    mArcSpec.setNoteOn(mCurPitchClass, gPositions);
    mCurPitchClass = PitchDetector::PitchClass::NONE;
  }
}

void MainComponent::run() { 
  mFft.processBuffer(mFileBuffer);
  mArcSpec.loadBuffer(&mFft.getSpectrum(), ArcSpectrogram::SpecType::SPECTROGRAM);
}

//==============================================================================
void MainComponent::prepareToPlay(int samplesPerBlockExpected,
                                  double sampleRate) {
  mSampleRate = sampleRate;
  mArcSpec.setSampleRate(sampleRate);
  mMidiCollector.reset(sampleRate);
}

void MainComponent::getNextAudioBlock(
    const juce::AudioSourceChannelInfo& bufferToFill) {
  bufferToFill.clearActiveBufferRegion();

  // fill a midi buffer with incoming messages from the midi input.
  juce::MidiBuffer incomingMidi;
  mMidiCollector.removeNextBlockOfMessages(incomingMidi,
                                           bufferToFill.numSamples);
  mKeyboardState.processNextMidiBuffer(incomingMidi, 0, bufferToFill.numSamples,
                                       true);
  if (!incomingMidi.isEmpty()) {
    for (juce::MidiMessageMetadata md : incomingMidi) {
      if (md.getMessage().isNoteOn() && mFileLoaded) {
        mCurPitchClass =
            (PitchDetector::PitchClass)md.getMessage().getNoteNumber();
      } else if (md.getMessage().isNoteOff() && mFileLoaded) {
        mSynth.stopNote(
            (PitchDetector::PitchClass)md.getMessage().getNoteNumber());
        mArcSpec.setNoteOff();
      }
    }
  }

  mSynth.process(bufferToFill.buffer);
}

void MainComponent::releaseResources() {
  // This will be called when the audio device stops, or when it is being
  // restarted due to a setting change.

  // For more details, see the help for AudioProcessor::releaseResources()
}

//==============================================================================
void MainComponent::paint(juce::Graphics& g) {
  // (Our component is opaque, so we must completely fill the background with a
  // solid colour)
  g.fillAll(juce::Colours::black);
}

void MainComponent::resized() {
  auto r = getLocalBounds();

  // auto titleSection = r.removeFromTop(LOGO_HEIGHT);
  // mLogo.setBounds(titleSection.withSizeKeepingCentre(LOGO_HEIGHT * 2,
  //                                                   titleSection.getHeight()));
  mKeyboard.setBounds(r.removeFromBottom(KEYBOARD_HEIGHT)
                          .withSizeKeepingCentre(PANEL_WIDTH, KEYBOARD_HEIGHT));

  // Left Panel
  auto leftPanel = r.removeFromLeft(PANEL_WIDTH);
  mBtnOpenFile.setBounds(leftPanel.removeFromTop(KNOB_HEIGHT));
  mBtnRecord.setBounds(leftPanel.removeFromTop(KNOB_HEIGHT));
  leftPanel.removeFromTop(ROW_PADDING_HEIGHT);
  // Row 1
  auto row =
      leftPanel.removeFromTop(KNOB_HEIGHT + LABEL_HEIGHT + ROW_PADDING_HEIGHT);
  // Diversity
  auto knob = row.removeFromLeft(row.getWidth() / 2);
  mSliderDiversity.setBounds(
      knob.withSize(KNOB_HEIGHT * 2, KNOB_HEIGHT)
          .withCentre(knob.getPosition().translated(
              knob.getWidth() / 2, (knob.getHeight() - LABEL_HEIGHT) / 2)));
  mLabelDiversity.setBounds(knob.removeFromBottom(LABEL_HEIGHT));
  // Rate
  knob = row;
  mSliderRate.setBounds(
      knob.withSize(KNOB_HEIGHT * 2, KNOB_HEIGHT)
          .withCentre(knob.getPosition().translated(
              knob.getWidth() / 2, (knob.getHeight() - LABEL_HEIGHT) / 2)));
  mLabelRate.setBounds(knob.removeFromBottom(LABEL_HEIGHT));
  // Row 2
  row =
      leftPanel.removeFromTop(KNOB_HEIGHT + LABEL_HEIGHT + ROW_PADDING_HEIGHT);
  // Rate
  knob = row;
  mSliderDuration.setBounds(
      knob.withSize(KNOB_HEIGHT * 2, KNOB_HEIGHT)
          .withCentre(knob.getPosition().translated(
              knob.getWidth() / 2, (knob.getHeight() - LABEL_HEIGHT) / 2)));
  mLabelDuration.setBounds(knob.removeFromBottom(LABEL_HEIGHT));

  auto rightPanel = r.removeFromRight(PANEL_WIDTH);

  mArcSpec.setBounds(r.removeFromBottom(r.getWidth() / 2.0f));
  mProgressBar.setBounds(
      mArcSpec.getBounds().withSizeKeepingCentre(PROGRESS_SIZE, PROGRESS_SIZE));
}

void MainComponent::openNewFile() {
  shutdownAudio();

  juce::FileChooser chooser("Select a file to granulize...",
                            juce::File("C:/users/brady/Music"), "*.wav;*.mp3",
                            false);

  if (chooser.browseForFileToOpen()) {
    auto file = chooser.getResult();
    std::unique_ptr<juce::AudioFormatReader> reader(
        mFormatManager.createReaderFor(file));

    if (reader.get() != nullptr) {
      auto duration = (float)reader->lengthInSamples / reader->sampleRate;
      mFileBuffer.setSize(reader->numChannels, (int)reader->lengthInSamples);

      juce::AudioBuffer<float> tempBuffer = juce::AudioBuffer<float>(
          reader->numChannels, (int)reader->lengthInSamples);
      reader->read(&tempBuffer, 0, (int)reader->lengthInSamples, 0, true, true);
      juce::ScopedPointer<juce::LagrangeInterpolator> resampler =
          new juce::LagrangeInterpolator();
      double ratio = reader->sampleRate / mSampleRate;
      const float** inputs = tempBuffer.getArrayOfReadPointers();
      float** outputs = mFileBuffer.getArrayOfWritePointers();
      for (int c = 0; c < mFileBuffer.getNumChannels(); c++) {
        resampler->reset();
        resampler->process(ratio, inputs[c], outputs[c],
                           mFileBuffer.getNumSamples());
      }
      mArcSpec.resetBuffers();
      stopThread(4000);
      startThread(); // process fft and pass to arc spec
      //mTransientDetector.processBuffer(&mFileBuffer);
      mPitchDetector.processBuffer(&mFileBuffer, mSampleRate);
      mSynth.setFileBuffer(&mFileBuffer, mSampleRate);
      mFileLoaded = false;
    }
  }
  setAudioChannels(2, 2);
}