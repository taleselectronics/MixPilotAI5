#pragma once
#include "PluginProcessor.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <memory>
#include <atomic>

class MixPilotEditor;
class Visualizer final : public juce::Component
{
public:
    explicit Visualizer(MixPilotEditor&);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    std::array<float,128> spectrum{};
private:
    MixPilotEditor& owner;
    juce::Rectangle<float> plottingRect() const;
    void drag(const juce::MouseEvent&);
};
class MixPilotEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MixPilotEditor(MixPilotProcessor&);
    ~MixPilotEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    MixPilotProcessor& processor;
    int selectedSlot=0;
private:
    Visualizer graph;
    juce::dsp::FFT fft{11};
    std::array<float,4096> fftData{};
    juce::Label title,tagline,rackTitle,graphTitle,detailTitle,status,sourceStatus,referenceStatus,meters,diagnostics;
    juce::Label sourceLabel,micLabel,slotLabel;
    juce::Label paramLabels[6];
    juce::TextEditor promptEditor;
    juce::TextButton scan{"ANALYZE LIVE"},sourceButton{"SOURCE WAV"},referenceButton{"REFERENCE WAV"},generate{"AI GENERATE"};
    juce::TextButton slots[MixPilotProcessor::slots];
    juce::TextButton bypass{"BYPASS"},up{"MOVE LEFT"},down{"MOVE RIGHT"},remove{"REMOVE"};
    juce::ComboBox source,mic,addModule;
    juce::Slider controls[6],master,wet,intensity;
    juce::Label masterLabel,wetLabel,intensityLabel;
    using SliderAttach=juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttach=juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::array<std::unique_ptr<SliderAttach>,6> controlAttachments;
    std::unique_ptr<SliderAttach> masterAttach,wetAttach,intensityAttach;
    std::unique_ptr<ComboAttach> sourceAttach,micAttach;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::ThreadPool inferencePool{1};
    juce::CriticalSection responseMutex;
    juce::String pendingResponse;
    std::atomic<bool> responseReady{false},requestActive{false};
    bool waitingForAnalysis=false;
    void timerCallback() override;
    void selectSlot(int);
    void updateControls();
    void refreshRack();
    bool analyzeFile(const juce::File&,bool);
    void requestMix();
    void deliverResponse(juce::String);
    void applyResponse(const juce::String&);
    static juce::String callLocalModel(const juce::String&,const ToneFeatures&,const ToneFeatures&,int,int,ResonanceHint);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixPilotEditor)
};
