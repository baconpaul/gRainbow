/*
  ==============================================================================

    ArcSpectrogram.cpp
    Created: 31 Mar 2021 10:18:28pm
    Author:  brady

  ==============================================================================
*/

#define _USE_MATH_DEFINES

#include "ArcSpectrogram.h"

#include <JuceHeader.h>
#include <limits.h>
#include <math.h>

#include "Utils.h"

//==============================================================================
ArcSpectrogram::ArcSpectrogram()
    : mSpectrogramImage(juce::Image::RGB, 512, 512, true),
      juce::Thread("spectrogram thread"),
      mFft(FFT_SIZE, HOP_SIZE) {
  setFramesPerSecond(10);
}

ArcSpectrogram::~ArcSpectrogram() { stopThread(4000); }

void ArcSpectrogram::paint(juce::Graphics& g) {
  g.fillAll(juce::Colours::black);

  // Draw fft
  g.drawImageAt(mSpectrogramImage, 0, 0);

  // Draw position markers
  juce::Point<int> centerPoint = juce::Point<int>(getWidth() / 2, getHeight());
  g.setColour(juce::Colours::white);
  for (GrainPositionFinder::GrainPosition gPos : mGPositions) {
    auto startPoint = centerPoint.getPointOnCircumference(
        getHeight() / 4.0f, (1.5 * M_PI) + (gPos.posRatio * M_PI));
    auto endPoint = centerPoint.getPointOnCircumference(
        getHeight() - 17, (1.5 * M_PI) + (gPos.posRatio * M_PI));
    g.setColour(juce::Colours::goldenrod);
    //float thickness = std::abs(1.0f - gPos.pbRate) * 10.0f;
    g.drawLine(juce::Line<float>(startPoint, endPoint), 2.0f);
  }

  // Draw transient markers
  if (mTransients != nullptr) {
    g.setOpacity(0.7);
    g.setColour(juce::Colours::blue);
    for (int i = 0; i < mTransients->size(); ++i) {
      TransientDetector::Transient transient = mTransients->at(i);
      auto startPoint = centerPoint.getPointOnCircumference(
          getHeight() - 21, (1.5 * M_PI) + (transient.posRatio * M_PI));
      auto endPoint = centerPoint.getPointOnCircumference(
          getHeight() - 17, (1.5 * M_PI) + (transient.posRatio * M_PI));
      g.drawLine(juce::Line<float>(startPoint, endPoint), 1.0f);
    }
  }
}

void ArcSpectrogram::resized() {
  auto r = getLocalBounds();
  // Position markers around rainbow
  juce::Point<int> startPoint = juce::Point<int>(getWidth() / 2, getHeight());
  for (int i = 0; i < mPositionMarkers.size(); ++i) {
    float angleRad = (M_PI * mGPositions[i].posRatio) - (M_PI / 2.0f);
    juce::Point<float> p =
        startPoint.getPointOnCircumference(getHeight() - 10, getHeight() - 10, angleRad);
    mPositionMarkers[i]->setSize(10, 15);
    mPositionMarkers[i]->setCentrePosition(0, 0);
    mPositionMarkers[i]->setTransform(
        juce::AffineTransform::rotation(angleRad).translated(p));
  }
}

void ArcSpectrogram::run() {
  if (mFileBuffer != nullptr) {
    mFft.processBuffer(*mFileBuffer);
    auto spec = mFft.getSpectrum();
    if (spec.size() == 0 || threadShouldExit()) return;
    int startRadius = getHeight() / 4.0f;
    int endRadius = getHeight();
    int bowWidth = endRadius - startRadius;
    int height = juce::jmax(2.0f, bowWidth / (float)spec[0].size());
    int minBinIdx = (MIN_FREQ * FFT_SIZE) / mSampleRate;
    int maxBinIdx = (MAX_FREQ * FFT_SIZE) / mSampleRate;
    juce::Point<int> startPoint = juce::Point<int>(getWidth() / 2, getHeight());
    mSpectrogramImage =
        juce::Image(juce::Image::RGB, getWidth(), getHeight(), true);
    juce::Graphics g(mSpectrogramImage);

    for (auto i = 0; i < spec.size(); ++i) {
      if (threadShouldExit()) return;
      for (auto curRadius = startRadius; curRadius < endRadius; ++curRadius) {
        float arcLen = 2 * M_PI * curRadius;
        int pixPerEntry = arcLen / spec.size();
        float radPerc = (curRadius - startRadius) / (float)bowWidth;
        auto specRow = minBinIdx + ((maxBinIdx - minBinIdx) * radPerc);

        auto rainbowColour = Utils::getRainbowColour(1.0f - radPerc);
        g.setColour(rainbowColour);

        g.setOpacity(juce::jlimit(0.0f, 1.0f, spec[i][specRow]));

        float xPerc = (float)i / spec.size();
        float angleRad = (M_PI * xPerc) - (M_PI / 2.0f);
        int width = pixPerEntry + 6;

        juce::Point<float> p =
            startPoint.getPointOnCircumference(curRadius, curRadius, angleRad);
        juce::AffineTransform rotation = juce::AffineTransform();
        rotation = rotation.rotated(angleRad, p.x, p.y);
        juce::Rectangle<float> rect = juce::Rectangle<float>(width, height);
        rect = rect.withCentre(p);
        rect = rect.transformedBy(rotation);
        juce::Path rectPath;
        rectPath.addRectangle(rect);

        g.fillPath(rectPath, rotation);
      }
    }
  } else if (mLoadedBuffer != nullptr) {
    std::vector<std::vector<float>>& spec = *mLoadedBuffer;
    if (spec.size() == 0 || threadShouldExit()) return;
    int startRadius = getHeight() / 4.0f;
    int endRadius = getHeight();
    int bowWidth = endRadius - startRadius;
    int height = juce::jmax(2.0f, bowWidth / (float)spec[0].size());
    juce::Point<int> startPoint = juce::Point<int>(getWidth() / 2, getHeight());
    mSpectrogramImage =
        juce::Image(juce::Image::RGB, getWidth(), getHeight(), true);
    juce::Graphics g(mSpectrogramImage);

    for (auto i = 0; i < spec.size(); ++i) {
      if (threadShouldExit()) return;
      for (auto curRadius = startRadius; curRadius < endRadius; ++curRadius) {
        float arcLen = 2 * M_PI * curRadius;
        int pixPerEntry = arcLen / spec.size();
        float radPerc = (curRadius - startRadius) / (float)bowWidth;
        auto specRow = radPerc * spec[i].size();

        auto rainbowColour = Utils::getRainbowColour(1.0f - radPerc);
        g.setColour(rainbowColour);

        g.setOpacity(juce::jlimit(0.0f, 1.0f, spec[i][specRow]));

        float xPerc = (float)i / spec.size();
        float angleRad = (M_PI * xPerc) - (M_PI / 2.0f);
        int width = pixPerEntry + 6;

        juce::Point<float> p =
            startPoint.getPointOnCircumference(curRadius, curRadius, angleRad);
        juce::AffineTransform rotation = juce::AffineTransform();
        rotation = rotation.rotated(angleRad, p.x, p.y);
        juce::Rectangle<float> rect = juce::Rectangle<float>(width, height);
        rect = rect.withCentre(p);
        rect = rect.transformedBy(rotation);
        juce::Path rectPath;
        rectPath.addRectangle(rect);

        g.fillPath(rectPath, rotation);
      }
    }
  }
}

void ArcSpectrogram::processBuffer(
    juce::AudioBuffer<float>* fileBuffer, double sampleRate) {
  stopThread(4000);
  mFileBuffer = fileBuffer;
  mSampleRate = sampleRate;
  startThread();  // Update spectrogram image
}

void ArcSpectrogram::loadBuffer(std::vector<std::vector<float>>* buffer) {
  stopThread(4000);
  mLoadedBuffer = buffer;
  startThread();
}

void ArcSpectrogram::setTransients(
    std::vector<TransientDetector::Transient>* transients) {
  mTransients = transients;
}

void ArcSpectrogram::updatePositions(int midiNote,
    std::vector<GrainPositionFinder::GrainPosition> gPositions) {
  mGPositions = gPositions;
  mCurNote = midiNote;
  mGPositions = gPositions;
  mPositionMarkers.clear();
  for (int i = 0; i < gPositions.size(); ++i) {
    auto newItem = new PositionMarker(gPositions[i]);
    newItem->addListener(this);
    mPositionMarkers.add(newItem);
    addAndMakeVisible(newItem);
  }
  resized();
}

void ArcSpectrogram::buttonClicked(juce::Button* btn) {
  if (onPositionUpdated == nullptr) return;
  for (int i = 0; i < mPositionMarkers.size(); ++i) {
    if (btn == mPositionMarkers[i]) {
      auto gPos = mGPositions[i];
      gPos.isEnabled = btn->getToggleState();
      onPositionUpdated(mCurNote, gPos);
    }
  }
}