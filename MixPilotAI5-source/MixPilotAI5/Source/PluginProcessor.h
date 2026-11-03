#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <vector>

struct ToneFeatures { float rmsDb=-90, lowDb=-90, midDb=-90, highDb=-90, peakDb=-90; bool valid=false; };
struct ResonanceHint { float hz=0, prominenceDb=0; };

class MixPilotProcessor final : public juce::AudioProcessor
{
public:
    static constexpr int slots=10, spectrumSize=2048, diagnosticBands=9;
    // Type 0 is empty, 1 bell EQ, 2 high-pass, 3 low-pass, 4 compressor,
    // 5 de-esser, 6 saturation, 7 gain, 8 stereo width, 9 delay, 10 limiter.
    static constexpr int types=11;
    static juce::StringArray typeNames();
    MixPilotProcessor();
    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();
    void prepareToPlay(double, int) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override {return true;}
    const juce::String getName() const override {return JucePlugin_Name;}
    bool acceptsMidi() const override {return false;}
    bool producesMidi() const override {return false;}
    bool isMidiEffect() const override {return false;}
    double getTailLengthSeconds() const override {return 0;}
    int getNumPrograms() override {return 1;}
    int getCurrentProgram() override {return 0;}
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override {return {};}
    void changeProgramName(int,const juce::String&) override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*,int) override;
    juce::AudioProcessorValueTreeState apvts;
    float value(const juce::String&) const;
    void setValue(const juce::String&,float);
    void setSlotType(int,int);
    void swapSlots(int,int);
    void startAnalysis() noexcept;
    void setFileFeatures(ToneFeatures) noexcept;
    bool analysisComplete() const noexcept {return captured.load();}
    ToneFeatures features() const noexcept;
    ResonanceHint resonance() const noexcept {return {resonanceHz.load(),resonanceDb.load()};}
    void spectrum(std::array<float,spectrumSize>&) const noexcept;
    float inputPeak() const noexcept {return inLevel.load();}
    float outputPeak() const noexcept {return outLevel.load();}
    float reductionDb() const noexcept {return reduction.load();}
    double sampleRateHz() const noexcept {return sampleRate;}
    void setPrompt(juce::String);
    juce::String prompt() const;
    void setReference(ToneFeatures);
    ToneFeatures reference() const;
private:
    struct StageState {
        juce::IIRFilter filter, second;
        float envelope=0.0f, compGain=1.0f, sibilanceLp=0.0f;
        std::vector<float> delayBuffer;
        size_t delayPosition=0;
    };
    std::array<std::array<StageState,slots>,2> stage;
    std::array<std::array<juce::IIRFilter,diagnosticBands>,2> detector;
    double sampleRate=44100;
    int filterCountdown=0;
    std::atomic<bool> analysisRequested{false},captured{false};
    bool capturing=false;
    int64_t frames=0,targetFrames=0;
    double rmsSum=0,lowSum=0,midSum=0,highSum=0;
    std::array<double,diagnosticBands> diagnosticSum{};
    float peak=0, lowState[2]{}, highState[2]{};
    std::array<std::atomic<float>,5> measured, fileMeasured;
    std::atomic<bool> fileValid{false};
    std::atomic<float> resonanceHz{0},resonanceDb{0},inLevel{0},outLevel{0},reduction{0};
    std::array<std::atomic<float>,spectrumSize> ring;
    std::atomic<unsigned int> ringWrite{0};
    mutable juce::CriticalSection stateLock; // never acquired by processBlock
    juce::String promptText;
    ToneFeatures referenceFeatures;
    void updateFilters();
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixPilotProcessor)
};
