#include "PluginEditor.h"
#include <cmath>
#include <complex>
#include <algorithm>
#include <functional>
#include <utility>
#if JUCE_WINDOWS
 #include <windows.h>
 #include <winhttp.h>
#endif
namespace {
const auto bg=juce::Colour(0xff080c14), surface=juce::Colour(0xff101924),panel=juce::Colour(0xff162433);
const auto border=juce::Colour(0xff2a3a4c),text=juce::Colour(0xffedf6ff),muted=juce::Colour(0xff91a6ba);
const auto cyan=juce::Colour(0xff4ce5e8),purple=juce::Colour(0xffb282f7),magenta=juce::Colour(0xfff16cc0);
const auto green=juce::Colour(0xff86e8b2);
float dB(float x){return juce::Decibels::gainToDecibels(juce::jmax(x,1.e-9f));}
float param(MixPilotProcessor& p,int s,const char* key){return p.value("s"+juce::String(s+1)+key);}
juce::String key(int s,const char* what){return "s"+juce::String(s+1)+what;}
void label(juce::Label& l,const juce::String& t,float size=12,juce::Colour colour=text,bool bold=false){
    l.setText(t,juce::dontSendNotification);
    l.setFont(juce::Font(juce::FontOptions(size,bold?juce::Font::bold:juce::Font::plain)));
    l.setColour(juce::Label::textColourId,colour);
}
void button(juce::TextButton& b,bool primary=false){
    b.setColour(juce::TextButton::buttonColourId,primary?cyan:panel);
    b.setColour(juce::TextButton::textColourOffId,primary?bg:text);
}
void slider(juce::Slider& s){
    s.setSliderStyle(juce::Slider::LinearHorizontal);
    s.setTextBoxStyle(juce::Slider::TextBoxRight,false,73,25);
    s.setColour(juce::Slider::trackColourId,cyan);
    s.setColour(juce::Slider::thumbColourId,cyan);
    s.setColour(juce::Slider::backgroundColourId,border);
    s.setColour(juce::Slider::textBoxTextColourId,text);
    s.setColour(juce::Slider::textBoxOutlineColourId,border);
    s.setColour(juce::Slider::textBoxBackgroundColourId,surface);
}
float normalizedFreq(float f){return std::log(juce::jlimit(20.0f,20000.0f,f)/20.0f)/std::log(1000.0f);}
}
Visualizer::Visualizer(MixPilotEditor& p):owner(p){}
juce::Rectangle<float> Visualizer::plottingRect() const {return getLocalBounds().toFloat().reduced(55,37);}
void Visualizer::paint(juce::Graphics& g){
    g.fillAll(surface);
    auto r=plottingRect();
    const float freqMarks[]{20,50,100,200,500,1000,2000,5000,10000,20000};
    const char* freqLabels[]{"20","50","100","200","500","1k","2k","5k","10k","20k"};
    g.setFont(12);
    for(int i=0;i<10;++i){
        const float x=r.getX()+normalizedFreq(freqMarks[i])*r.getWidth();
        g.setColour(border);g.drawVerticalLine((int)x,r.getY(),r.getBottom());
        g.setColour(muted);g.drawText(freqLabels[i],(int)x-20,(int)r.getBottom()+9,40,17,juce::Justification::centred);
    }
    for(int db=-18;db<=18;db+=6){
        const float y=r.getCentreY()-db*r.getHeight()/36;
        g.setColour(db==0?border.brighter(.45f):border);g.drawHorizontalLine((int)y,r.getX(),r.getRight());
        g.setColour(muted);g.drawText(juce::String(db)+" dB",3,(int)y-8,48,16,juce::Justification::centredRight);
    }
    juce::Path spectrumPath;
    for(int i=0;i<128;++i){
        const float f=20*std::pow(1000.0f,i/127.0f);
        const float x=r.getX()+normalizedFreq(f)*r.getWidth();
        const float y=juce::jmap(juce::jlimit(-88.0f,-15.0f,spectrum[(size_t)i]),-88.0f,-15.0f,r.getBottom(),r.getY());
        if(i==0)spectrumPath.startNewSubPath(x,y);else spectrumPath.lineTo(x,y);
    }
    g.setColour(cyan.withAlpha(.29f));g.strokePath(spectrumPath,juce::PathStrokeType(2.0f));
    // Draw composite response of every enabled EQ and cutoff module.
    juce::Path curve;
    const auto rate=owner.processor.sampleRateHz();
    for(int col=0;col<=420;++col){
        const float f=20*std::pow(1000.0f,col/420.0f);
        const double w=2.0*juce::MathConstants<double>::pi*juce::jmin((double)f,rate*.49)/rate;
        const std::complex<double> z1(std::cos(w),-std::sin(w)),z2=z1*z1;
        double magnitude=1;
        for(int k=0;k<MixPilotProcessor::slots;++k){
            const int type=(int)param(owner.processor,k,"type");
            if(param(owner.processor,k,"enable")<.5f||type<1||type>3)continue;
            const double hz=juce::jlimit(20.0,rate*.44,(double)param(owner.processor,k,"freq"));
            juce::IIRCoefficients co;
            if(type==1)co=juce::IIRCoefficients::makePeakFilter(rate,hz,param(owner.processor,k,"q"),juce::Decibels::decibelsToGain(param(owner.processor,k,"gain")));
            else if(type==2)co=juce::IIRCoefficients::makeHighPass(rate,hz);
            else co=juce::IIRCoefficients::makeLowPass(rate,hz);
            const auto* a=co.coefficients;
            const std::complex<double> numerator=(double)a[0]+(double)a[1]*z1+(double)a[2]*z2;
            const std::complex<double> denominator=1.0+(double)a[3]*z1+(double)a[4]*z2;
            const double m=std::abs(numerator/denominator);
            magnitude*=((type!=1&&(int)param(owner.processor,k,"slope")==1)?m*m:m);
        }
        const float y=r.getCentreY()-juce::jlimit(-18.0f,18.0f,dB((float)magnitude))*r.getHeight()/36.0f;
        const float x=r.getX()+col*r.getWidth()/420;
        if(col==0)curve.startNewSubPath(x,y);else curve.lineTo(x,y);
    }
    g.setColour(purple.withAlpha(.18f));g.strokePath(curve,juce::PathStrokeType(9));
    g.setColour(cyan);g.strokePath(curve,juce::PathStrokeType(2.8f));
    for(int k=0;k<MixPilotProcessor::slots;++k){
        const int type=(int)param(owner.processor,k,"type");
        if(type<1||type>3||param(owner.processor,k,"enable")<.5f)continue;
        const float x=r.getX()+normalizedFreq(param(owner.processor,k,"freq"))*r.getWidth();
        const float gain=type==1?param(owner.processor,k,"gain"):0;
        const float y=r.getCentreY()-gain*r.getHeight()/36.0f;
        const bool selected=k==owner.selectedSlot;
        g.setColour(selected?cyan:purple);
        g.fillEllipse(x-(selected?10:7),y-(selected?10:7),selected?20.0f:14.0f,selected?20.0f:14.0f);
        g.setColour(bg);g.setFont(10);g.drawText(juce::String(k+1),(int)x-8,(int)y-8,16,16,juce::Justification::centred);
    }
    const int chosen=(int)param(owner.processor,owner.selectedSlot,"type");
    if(chosen==4||chosen==6){
        // Transfer curve illustrating compression or saturation of selected module.
        juce::Rectangle<float> mini(r.getRight()-132,r.getY()+10,116,92);
        g.setColour(bg.withAlpha(.7f));g.fillRoundedRectangle(mini,6);
        g.setColour(muted);g.drawRect(mini);
        juce::Path p;
        for(int i=0;i<=80;++i){
            const float x=i/80.0f;
            float y=x;
            if(chosen==6){const float drive=1+param(owner.processor,owner.selectedSlot,"amount")*.12f;y=std::tanh(x*drive)/std::tanh(drive);}
            else {const float threshold=juce::jmap(param(owner.processor,owner.selectedSlot,"amount")*.01f,.78f,.2f);const float ratio=param(owner.processor,owner.selectedSlot,"ratio");if(x>threshold)y=threshold+(x-threshold)/ratio;}
            const float xx=mini.getX()+x*mini.getWidth(),yy=mini.getBottom()-y*mini.getHeight();
            if(i==0)p.startNewSubPath(xx,yy);else p.lineTo(xx,yy);
        }
        g.setColour(magenta);g.strokePath(p,juce::PathStrokeType(2));
    }
}
void Visualizer::mouseDown(const juce::MouseEvent& e){drag(e);}
void Visualizer::mouseDrag(const juce::MouseEvent& e){drag(e);}
void Visualizer::drag(const juce::MouseEvent& e){
    const int type=(int)param(owner.processor,owner.selectedSlot,"type");
    if(type<1||type>3)return;
    auto r=plottingRect();
    const float fx=juce::jlimit(0.0f,1.0f,(e.position.x-r.getX())/r.getWidth());
    const float f=20*std::pow(1000.0f,fx);
    owner.processor.setValue(key(owner.selectedSlot,"freq"),f);
    if(type==1)owner.processor.setValue(key(owner.selectedSlot,"gain"),juce::jlimit(-18.0f,18.0f,(r.getCentreY()-e.position.y)*36/r.getHeight()));
    repaint();
}
MixPilotEditor::MixPilotEditor(MixPilotProcessor& p):AudioProcessorEditor(&p),processor(p),graph(*this){
    setSize(1260,870);
    label(title,"MIXPILOT  /  AI 5",31,text,true);
    label(tagline,"MODULAR MIXING STUDIO      LOCAL AI      LIVE SPECTRAL FEEDBACK",12,muted);
    label(rackTitle,"EFFECT CHAIN  /  drag-free reorder",15,text,true);
    label(graphTitle,"SPECTRUM  +  COMBINED FILTER RESPONSE",14,text,true);
    label(detailTitle,"MODULE SETTINGS",15,text,true);
    label(status,"Ready. Analyze a vocal before generating an AI mix.",12,green);
    label(sourceStatus,"SOURCE  •  live / WAV",11,muted);
    label(referenceStatus,"REFERENCE  •  none",11,muted);
    label(meters,"IN  -∞      OUT  -∞      GR  0 dB",13,text);
    label(diagnostics,"Resonance scan: awaiting analysis",12,muted);
    label(sourceLabel,"TRACK TYPE",11,muted);label(micLabel,"MIC PROFILE",11,muted);
    label(slotLabel,"ADD / CHANGE MODULE",11,muted);
    label(masterLabel,"OUTPUT",11,muted);label(wetLabel,"WET",11,muted);label(intensityLabel,"AI AMOUNT",11,muted);
    for(auto* l:{&title,&tagline,&rackTitle,&graphTitle,&detailTitle,&status,&sourceStatus,&referenceStatus,&meters,&diagnostics,&sourceLabel,&micLabel,&slotLabel,&masterLabel,&wetLabel,&intensityLabel})addAndMakeVisible(*l);
    promptEditor.setMultiLine(true);promptEditor.setReturnKeyStartsNewLine(true);
    const auto stored=processor.prompt();
    promptEditor.setText(stored.isEmpty()?"Make this vocal upfront and polished. Analyze measured tonal balance, control harshness, improve dynamics, preserve headroom.":stored);
    promptEditor.setColour(juce::TextEditor::backgroundColourId,surface);
    promptEditor.setColour(juce::TextEditor::textColourId,text);
    promptEditor.setColour(juce::TextEditor::outlineColourId,border);
    promptEditor.onTextChange=[this]{processor.setPrompt(promptEditor.getText());};addAndMakeVisible(promptEditor);
    for(auto* b:{&scan,&sourceButton,&referenceButton,&generate,&up,&down,&remove,&bypass}){button(*b,b==&generate);addAndMakeVisible(*b);}
    for(const auto& name:{"Universal","Vocals","Instrument","Drums","Stereo Mix"})source.addItem(name,source.getNumItems()+1);
    for(const auto& name:{"Unknown / Flat","Neumann TLM 103","Generic dynamic microphone"})mic.addItem(name,mic.getNumItems()+1);
    const auto types=MixPilotProcessor::typeNames();
    for(int i=0;i<types.size();++i)addModule.addItem(types[i],i+1);
    for(auto* cb:{&source,&mic,&addModule}){
        cb->setColour(juce::ComboBox::backgroundColourId,panel);
        cb->setColour(juce::ComboBox::textColourId,text);
        cb->setColour(juce::ComboBox::outlineColourId,border);
        addAndMakeVisible(*cb);
    }
    sourceAttach=std::make_unique<ComboAttach>(processor.apvts,"mode",source);
    micAttach=std::make_unique<ComboAttach>(processor.apvts,"mic",mic);
    addModule.onChange=[this]{processor.setSlotType(selectedSlot,addModule.getSelectedId()-1);updateControls();refreshRack();};
    for(int i=0;i<MixPilotProcessor::slots;++i){
        button(slots[i]);addAndMakeVisible(slots[i]);slots[i].onClick=[this,i]{selectSlot(i);};
    }
    addAndMakeVisible(graph);
    const char* ids[]{"freq","gain","q","amount","ratio","attack"};
    for(int i=0;i<6;++i){slider(controls[i]);addAndMakeVisible(controls[i]);label(paramLabels[i],juce::String(ids[i]).toUpperCase(),11,muted);addAndMakeVisible(paramLabels[i]);}
    for(auto* s:{&master,&wet,&intensity}){slider(*s);addAndMakeVisible(*s);}
    masterAttach=std::make_unique<SliderAttach>(processor.apvts,"output",master);
    wetAttach=std::make_unique<SliderAttach>(processor.apvts,"wet",wet);
    intensityAttach=std::make_unique<SliderAttach>(processor.apvts,"intensity",intensity);
    scan.onClick=[this]{processor.startAnalysis();waitingForAnalysis=true;status.setText("Listening to six seconds of live input...",juce::dontSendNotification);};
    sourceButton.onClick=[this]{
        chooser=std::make_unique<juce::FileChooser>("Choose source WAV",juce::File(),"*.wav");
        chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[this](const juce::FileChooser& fc){
            const auto file=fc.getResult();if(file.existsAsFile())status.setText(analyzeFile(file,false)?"Source analyzed — ready for AI.":"Unable to read source WAV.",juce::dontSendNotification);
        });
    };
    referenceButton.onClick=[this]{
        chooser=std::make_unique<juce::FileChooser>("Choose reference WAV",juce::File(),"*.wav");
        chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[this](const juce::FileChooser& fc){
            const auto file=fc.getResult();if(file.existsAsFile())status.setText(analyzeFile(file,true)?"Reference measured. This is broad tonal matching only.":"Unable to read reference WAV.",juce::dontSendNotification);
        });
    };
    generate.onClick=[this]{requestMix();};
    up.onClick=[this]{if(selectedSlot>0){processor.swapSlots(selectedSlot,selectedSlot-1);selectSlot(selectedSlot-1);}};
    down.onClick=[this]{if(selectedSlot<MixPilotProcessor::slots-1){processor.swapSlots(selectedSlot,selectedSlot+1);selectSlot(selectedSlot+1);}};
    remove.onClick=[this]{processor.setSlotType(selectedSlot,0);updateControls();refreshRack();};
    bypass.onClick=[this]{processor.setValue(key(selectedSlot,"enable"),1-param(processor,selectedSlot,"enable"));refreshRack();};
    selectSlot(0);
    startTimerHz(12);
}
MixPilotEditor::~MixPilotEditor(){stopTimer();chooser.reset();inferencePool.removeAllJobs(true,30000);}
void MixPilotEditor::selectSlot(int i){
    selectedSlot=juce::jlimit(0,MixPilotProcessor::slots-1,i);
    addModule.onChange=nullptr;
    addModule.setSelectedId((int)param(processor,selectedSlot,"type")+1,juce::dontSendNotification);
    addModule.onChange=[this]{processor.setSlotType(selectedSlot,addModule.getSelectedId()-1);updateControls();refreshRack();};
    updateControls();refreshRack();graph.repaint();
}
void MixPilotEditor::updateControls(){
    for(auto& a:controlAttachments)a.reset();
    const int type=(int)param(processor,selectedSlot,"type");
    const char* names[6]{"freq","gain","q","amount","ratio","release"};
    const char* titles[6]{"FREQUENCY / CUTOFF (Hz)","GAIN (dB)","Q / BANDWIDTH","STRENGTH (%)","RATIO","RELEASE (ms)"};
    if(type==4){names[0]="amount";names[1]="ratio";names[2]="attack";names[3]="release";}
    if(type==5){names[0]="freq";names[1]="amount";names[2]="attack";names[3]="release";}
    if(type==6||type==8||type==10){names[0]="amount";names[1]="mix";}
    if(type==9){names[0]="release";names[1]="amount";names[2]="mix";}
    if(type==7){names[0]="gain";names[1]="mix";}
    if(type==2||type==3){names[0]="freq";names[1]="slope";names[2]="mix";}
    int count=0;
    if(type==1){names[3]="mix";count=4;}
    if(type==2||type==3)count=3;
    if(type==4||type==5)count=4;
    if(type==6||type==7||type==8||type==10)count=2;
    if(type==9)count=3;
    for(int k=0;k<6;++k){
        const bool visible=k<count;
        controls[k].setVisible(visible);paramLabels[k].setVisible(visible);
        if(!visible)continue;
        const juce::String name(names[k]);
        juce::String caption=name.toUpperCase();
        if(name=="freq")caption="FREQUENCY / CUTOFF (Hz)";
        if(name=="gain")caption="GAIN (dB)";
        if(name=="q")caption="Q / WIDTH";
        if(name=="amount")caption="STRENGTH (%)";
        if(name=="attack"||name=="release")caption=name.toUpperCase()+" (ms)";
        if(name=="slope")caption="SLOPE (0 = 12 dB, 1 = 24 dB)";
        if(name=="mix")caption="MODULE MIX (%)";
        label(paramLabels[k],caption,11,muted);
        controlAttachments[(size_t)k]=std::make_unique<SliderAttach>(processor.apvts,key(selectedSlot,names[k]),controls[k]);
    }
    bypass.setButtonText(param(processor,selectedSlot,"enable")>.5f?"BYPASS OFF":"BYPASS ON");
    detailTitle.setText("MODULE "+juce::String(selectedSlot+1)+"  /  "+MixPilotProcessor::typeNames()[(int)param(processor,selectedSlot,"type")],juce::dontSendNotification);
}
void MixPilotEditor::refreshRack(){
    const auto names=MixPilotProcessor::typeNames();
    for(int i=0;i<MixPilotProcessor::slots;++i){
        const int type=(int)param(processor,i,"type");
        const bool enabled=param(processor,i,"enable")>.5f;
        auto& b=slots[i];
        b.setButtonText(juce::String(i+1)+"  "+names[type]+(enabled?"":"  [OFF]"));
        b.setColour(juce::TextButton::buttonColourId,i==selectedSlot?cyan:type==0?surface:panel);
        b.setColour(juce::TextButton::textColourOffId,i==selectedSlot?bg:enabled?text:muted);
    }
    bypass.setButtonText(param(processor,selectedSlot,"enable")>.5f?"BYPASS OFF":"BYPASS ON");
}
void MixPilotEditor::paint(juce::Graphics& g){
    g.fillAll(bg);
    g.setColour(cyan);g.fillRoundedRectangle(28,22,6,47,3);
    auto card=[&](float x,float y,float w,float h){g.setColour(surface);g.fillRoundedRectangle(x,y,w,h,13);g.setColour(border);g.drawRoundedRectangle(x,y,w,h,13,1);};
    card(22,90,1216,176); card(22,282,1216,140); card(22,438,828,360);card(864,438,374,360);
    card(22,815,1216,43);
    g.setColour(purple);g.fillRoundedRectangle(25,283,1210,3,1);
    g.setColour(cyan);g.fillRoundedRectangle(24,437,820,3,1);
    g.setColour(magenta);g.fillRoundedRectangle(864,437,370,3,1);
    g.setColour(muted);g.setFont(10);g.drawText("RUNS LOCALLY  •  LM STUDIO 127.0.0.1:1235  •  REAL-TIME DSP",810,26,400,18,juce::Justification::centredRight);
}
void MixPilotEditor::resized(){
    title.setBounds(45,20,460,50);tagline.setBounds(44,68,700,18);
    promptEditor.setBounds(42,111,710,83);
    sourceLabel.setBounds(779,103,200,18);source.setBounds(780,125,202,30);
    micLabel.setBounds(997,103,190,18);mic.setBounds(997,125,214,30);
    scan.setBounds(42,207,150,40);sourceButton.setBounds(202,207,150,40);
    referenceButton.setBounds(362,207,150,40);generate.setBounds(522,207,230,40);
    status.setBounds(780,164,433,45);
    sourceStatus.setBounds(780,215,433,18);referenceStatus.setBounds(780,235,433,18);
    rackTitle.setBounds(40,291,600,22);
    for(int i=0;i<MixPilotProcessor::slots;++i)
        slots[i].setBounds(38+(i%5)*239,321+(i/5)*48,226,38);
    graphTitle.setBounds(42,446,560,30);
    graph.setBounds(28,480,812,304);
    detailTitle.setBounds(884,446,330,27);
    slotLabel.setBounds(888,481,310,16);addModule.setBounds(888,501,329,31);
    bypass.setBounds(888,548,101,29);up.setBounds(995,548,104,29);down.setBounds(1105,548,110,29);
    remove.setBounds(888,585,329,27);
    for(int i=0;i<6;++i){
        const int col=i%2,row=i/2;
        paramLabels[i].setBounds(887+col*166,622+row*51,162,17);
        controls[i].setBounds(886+col*166,640+row*51,160,26);
    }
    meters.setBounds(42,818,400,26);diagnostics.setBounds(445,818,402,26);
    masterLabel.setBounds(866,820,70,18);master.setBounds(911,817,101,25);
    wetLabel.setBounds(1018,820,40,18);wet.setBounds(1044,817,90,25);
    intensityLabel.setBounds(1135,820,95,18);intensity.setBounds(1180,817,46,25);
}
void MixPilotEditor::timerCallback(){
    meters.setText("IN "+juce::String(dB(processor.inputPeak()),1)+" dB    OUT "+juce::String(dB(processor.outputPeak()),1)+" dB    GR "+juce::String(processor.reductionDb(),1)+" dB",juce::dontSendNotification);
    if(responseReady.exchange(false)){
        juce::String reply;
        {juce::ScopedLock l(responseMutex);reply=pendingResponse;}
        applyResponse(reply);requestActive.store(false);
    }
    if(waitingForAnalysis&&processor.analysisComplete()){
        waitingForAnalysis=false;auto f=processor.features();
        status.setText("Measured live audio RMS "+juce::String(f.rmsDb,1)+" dBFS. Ready for AI.",juce::dontSendNotification);
        auto res=processor.resonance();
        diagnostics.setText(res.hz>0?"POSSIBLE peak: "+juce::String((int)res.hz)+" Hz, prominence "+juce::String(res.prominenceDb,1)+" dB (verify by ear)":"No narrow-band candidate identified",juce::dontSendNotification);
    }
    std::array<float,MixPilotProcessor::spectrumSize> samples{};
    processor.spectrum(samples);
    fftData.fill(0);
    constexpr int n=MixPilotProcessor::spectrumSize;
    for(int i=0;i<n;++i)fftData[(size_t)i]=samples[(size_t)i]*(.5f-.5f*std::cos(2*juce::MathConstants<double>::pi*i/(n-1)));
    fft.performFrequencyOnlyForwardTransform(fftData.data(),true);
    for(int i=0;i<128;++i){
        const float f=20*std::pow(1000.0f,i/127.0f);
        const int bin=juce::jlimit(1,n/2-1,(int)std::round(f*n/processor.sampleRateHz()));
        const float mag=juce::jmax(fftData[(size_t)bin],fftData[(size_t)juce::jmin(n/2-1,bin+1)]);
        const float level=dB(mag/n*4);
        graph.spectrum[(size_t)i]=graph.spectrum[(size_t)i]*.7f+level*.3f;
    }
    graph.repaint();
}
bool MixPilotEditor::analyzeFile(const juce::File& file,bool reference){
    juce::AudioFormatManager formats;formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if(!reader||reader->sampleRate<8000||reader->numChannels<1)return false;
    const auto length=juce::jmin(reader->lengthInSamples,(juce::int64)(reader->sampleRate*180.0));
    if(length<1)return false;
    juce::AudioBuffer<float> audio(juce::jmin(2,(int)reader->numChannels),8192);
    const float lowCoefficient=std::exp(-2.0*juce::MathConstants<double>::pi*210.0/reader->sampleRate);
    const float highCoefficient=std::exp(-2.0*juce::MathConstants<double>::pi*4700.0/reader->sampleRate);
    float lp[2]{},hp[2]{},peak=0;
    double rms=0,low=0,mid=0,high=0,total=0;
    for(juce::int64 pos=0;pos<length;pos+=audio.getNumSamples()){
        const int count=(int)juce::jmin((juce::int64)audio.getNumSamples(),length-pos);
        if(!reader->read(&audio,0,count,pos,true,true))return false;
        for(int c=0;c<audio.getNumChannels();++c){
            const auto* samples=audio.getReadPointer(c);
            for(int i=0;i<count;++i){
                const float x=samples[i];
                lp[c]=lowCoefficient*lp[c]+(1-lowCoefficient)*x;
                hp[c]=highCoefficient*hp[c]+(1-highCoefficient)*x;
                const float lo=lp[c],hi=x-hp[c],md=x-lo-hi;
                rms+=x*x;low+=lo*lo;mid+=md*md;high+=hi*hi;
                peak=juce::jmax(peak,std::abs(x));++total;
            }
        }
    }
    if(total<1)return false;
    ToneFeatures f;
    f.rmsDb=dB((float)std::sqrt(rms/total));f.lowDb=dB((float)std::sqrt(low/total));
    f.midDb=dB((float)std::sqrt(mid/total));f.highDb=dB((float)std::sqrt(high/total));
    f.peakDb=dB(peak);f.valid=true;
    if(reference){processor.setReference(f);referenceStatus.setText("REFERENCE  •  "+file.getFileName(),juce::dontSendNotification);}
    else {processor.setFileFeatures(f);sourceStatus.setText("SOURCE  •  "+file.getFileName(),juce::dontSendNotification);}
    return true;
}
void MixPilotEditor::deliverResponse(juce::String value){juce::ScopedLock l(responseMutex);pendingResponse=std::move(value);responseReady.store(true);}
juce::String MixPilotEditor::callLocalModel(const juce::String& instruction,const ToneFeatures& audio,const ToneFeatures& ref,int mic,int mode,ResonanceHint suspect){
    const juce::String micText=mic==1?"Neumann TLM 103":mic==2?"generic dynamic mic":"unknown";
    const juce::String source=juce::StringArray{"Universal","Vocals","Instrument","Drums","Stereo Mix"}[juce::jlimit(0,4,mode)];
    juce::String measurement="Actual measured source (not direct AI hearing): "+source+
        ", mic="+micText+", RMS="+juce::String(audio.rmsDb,2)+" dBFS, peak="+juce::String(audio.peakDb,2)+
        " dBFS, low (<210Hz)="+juce::String(audio.lowDb,2)+", mid (210-4700Hz)="+juce::String(audio.midDb,2)+
        ", high (>4700Hz)="+juce::String(audio.highDb,2)+" dBFS.";
    if(suspect.hz>0)measurement+=" Possible resonance at "+juce::String(suspect.hz,1)+"Hz, prominence "+juce::String(suspect.prominenceDb,1)+"dB. Low confidence.";
    if(ref.valid)measurement+=" Reference RMS "+juce::String(ref.rmsDb,2)+", low "+juce::String(ref.lowDb,2)+", mid "+juce::String(ref.midDb,2)+", high "+juce::String(ref.highDb,2)+" dBFS.";
    measurement+=" User target: "+instruction;
    const juce::String system=R"(You are a local, measurement-aware music mixing planner. You have numeric features only, not direct access to source audio; don't pretend to hear it. Respond ONLY with ONE short valid JSON object, no markdown, no explanation outside JSON. Shape: {"modules":[{"type":2,"freq":80},{"type":1,"freq":300,"gain":-2,"q":1.3},{"type":4,"amount":42,"ratio":3,"attack":12,"release":110},{"type":5,"freq":6500,"amount":25},{"type":6,"amount":8},{"type":3,"freq":18000}],"explanation":"short, evidence-based explanation"}. Module types: 1 parametric EQ, 2 low cut / high pass, 3 high cut / low pass, 4 compressor, 5 de-esser, 6 saturator, 7 gain, 8 stereo width, 9 echo delay, 10 soft limiter. At most 10 modules. Duplicates are explicitly supported; use two compressors or EQs if warranted. All fields optional except type. Allowed numeric ranges: freq 20..20000, gain -18..18, q 0.3..12, amount 0..100, ratio 1..12, attack 0.1..200ms, release 10..1000ms, mix 0..100, slope 0 or 1 (12/24 dB per octave). Use gentle settings unless the user explicitly asks for a stronger sound. Use measured features, flag uncertain resonance suggestions in the explanation. TLM 103 mic identity is not proof of a defect. Keep headroom. Never claim an exact artist vocal match. Emit only JSON.)";
    juce::DynamicObject::Ptr payload=new juce::DynamicObject();
    payload->setProperty("model","qwen/qwen3.5-9b");
    payload->setProperty("max_tokens",8192);
    payload->setProperty("temperature",0.2);
    payload->setProperty("stream",false);
    juce::Array<juce::var> messages;
    for(const auto& pair:{std::pair<juce::String,juce::String>{"system",system},{"user",measurement}}){
        juce::DynamicObject::Ptr o=new juce::DynamicObject();
        o->setProperty("role",pair.first);o->setProperty("content",pair.second);
        messages.add(juce::var(o.get()));
    }
    payload->setProperty("messages",juce::var(messages));
    const std::string body=juce::JSON::toString(juce::var(payload.get())).toStdString();
    juce::String response;
    int code=0;
#if JUCE_WINDOWS
    struct Handle{HINTERNET p=nullptr;~Handle(){if(p)WinHttpCloseHandle(p);}};
    Handle session,connection,request;
    session.p=WinHttpOpen(L"MixPilotAI5/5.0",WINHTTP_ACCESS_TYPE_NO_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
    if(!session.p)return "ERROR: Cannot create Windows HTTP session.";
    WinHttpSetTimeouts(session.p,5000,5000,10000,180000);
    connection.p=WinHttpConnect(session.p,L"127.0.0.1",1235,0);
    if(!connection.p)return "ERROR: Cannot reach LM Studio at 127.0.0.1:1235.";
    request.p=WinHttpOpenRequest(connection.p,L"POST",L"/v1/chat/completions",nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,0);
    if(!request.p)return "ERROR: Could not create LM Studio request.";
    const wchar_t* headers=L"Content-Type: application/json\r\nAccept: application/json\r\n";
    const DWORD length=(DWORD)body.size();
    if(!WinHttpSendRequest(request.p,headers,(DWORD)-1,const_cast<char*>(body.data()),length,length,(DWORD_PTR)0))
        return "ERROR: WinHTTP send error "+juce::String((int)GetLastError());
    if(!WinHttpReceiveResponse(request.p,nullptr))return "ERROR: LM Studio response error "+juce::String((int)GetLastError());
    DWORD status=0,statusSize=sizeof(status);
    if(WinHttpQueryHeaders(request.p,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&statusSize,WINHTTP_NO_HEADER_INDEX))code=(int)status;
    std::string raw;
    while(raw.size()<4*1024*1024){
        char buffer[8192];DWORD count=0;
        if(!WinHttpReadData(request.p,buffer,sizeof(buffer),&count))return "ERROR: LM Studio read error "+juce::String((int)GetLastError());
        if(count==0)break;
        raw.append(buffer,(size_t)count);
    }
    response=juce::String::fromUTF8(raw.data(),(int)raw.size());
#else
    return "ERROR: This local inference bridge currently targets Windows.";
#endif
    if(code<200||code>=300)return "ERROR: LM Studio HTTP "+juce::String(code)+" "+response.substring(0,150);
    auto parsed=juce::JSON::parse(response);
    if(auto* root=parsed.getDynamicObject()){
        auto a=root->getProperty("choices");
        if(auto* choices=a.getArray())if(!choices->isEmpty()){
            auto* choice=choices->getReference(0).getDynamicObject();
            if(choice)if(auto* message=choice->getProperty("message").getDynamicObject()){
                const auto finalText=message->getProperty("content").toString();
                if(finalText.isNotEmpty())return finalText;
                return "ERROR: Model returned no final text; check LM Studio reasoning/token settings.";
            }
        }
    }
    return "ERROR: Invalid LM Studio response.";
}
void MixPilotEditor::requestMix(){
    const auto measured=processor.features();
    if(!measured.valid){status.setText("Analyze live audio or load source WAV first.",juce::dontSendNotification);return;}
    if(requestActive.exchange(true)){status.setText("AI request already running.",juce::dontSendNotification);return;}
    const auto ref=processor.reference();
    const auto micId=(int)processor.value("mic"),sourceId=(int)processor.value("mode");
    const auto resonance=processor.resonance();
    const auto target=promptEditor.getText();
    status.setText("Qwen planning a measurement-aware module chain...",juce::dontSendNotification);
    class Job final : public juce::ThreadPoolJob{
    public:
        Job(std::function<juce::String()> f,std::function<void(juce::String)> cb):ThreadPoolJob("MixPilot inference"),work(std::move(f)),done(std::move(cb)){}
        JobStatus runJob() override {auto result=work();if(!shouldExit())done(std::move(result));return jobHasFinished;}
    private:
        std::function<juce::String()> work;
        std::function<void(juce::String)> done;
    };
    inferencePool.addJob(new Job([target,measured,ref,micId,sourceId,resonance]{return callLocalModel(target,measured,ref,micId,sourceId,resonance);},
                                 [this](juce::String s){deliverResponse(std::move(s));}),true);
}
void MixPilotEditor::applyResponse(const juce::String& result){
    if(result.startsWith("ERROR:")){status.setText(result,juce::dontSendNotification);return;}
    const int first=result.indexOfChar('{'),last=result.lastIndexOfChar('}');
    if(first<0||last<=first){status.setText("AI returned no JSON. Existing chain preserved.",juce::dontSendNotification);return;}
    const auto doc=juce::JSON::parse(result.substring(first,last+1));
    auto* o=doc.getDynamicObject();
    if(!o){status.setText("AI returned invalid JSON. Existing chain preserved.",juce::dontSendNotification);return;}
    auto v=o->getProperty("modules");
    auto* arr=v.getArray();
    if(!arr||arr->isEmpty()||arr->size()>MixPilotProcessor::slots){status.setText("AI module list invalid. Existing chain preserved.",juce::dontSendNotification);return;}
    struct Mod{int type=0;float freq=1200,gain=0,q=1.1f,amount=30,ratio=3,attack=12,release=110,mix=100,slope=0;};
    std::array<Mod,MixPilotProcessor::slots> proposed{};
    auto number=[](juce::var v,float lo,float hi,float& target){
        if(v.isVoid())return true;
        if(!(v.isInt()||v.isInt64()||v.isDouble()))return false;
        const float value=(float)(double)v;
        if(!std::isfinite(value))return false;
        target=juce::jlimit(lo,hi,value);return true;
    };
    for(int i=0;i<arr->size();++i){
        auto* module=arr->getReference(i).getDynamicObject();
        if(!module){status.setText("AI module malformed. Chain unchanged.",juce::dontSendNotification);return;}
        float type=-1;
        if(!number(module->getProperty("type"),1,10,type)||type<1||type>10){status.setText("AI module type invalid. Chain unchanged.",juce::dontSendNotification);return;}
        auto& m=proposed[(size_t)i];m.type=(int)type;
        if(!number(module->getProperty("freq"),20,20000,m.freq)||
           !number(module->getProperty("gain"),-18,18,m.gain)||
           !number(module->getProperty("q"),.3f,12,m.q)||
           !number(module->getProperty("amount"),0,100,m.amount)||
           !number(module->getProperty("ratio"),1,12,m.ratio)||
           !number(module->getProperty("attack"),.1f,200,m.attack)||
           !number(module->getProperty("release"),10,1000,m.release)||
           !number(module->getProperty("mix"),0,100,m.mix)||
           !number(module->getProperty("slope"),0,1,m.slope)){
            status.setText("AI parameter invalid. Chain unchanged.",juce::dontSendNotification);return;
        }
    }
    const float strength=processor.value("intensity")*.01f;
    for(int i=0;i<MixPilotProcessor::slots;++i){
        const auto& m=proposed[(size_t)i];
        processor.setValue(key(i,"type"),(float)m.type);
        processor.setValue(key(i,"enable"),1);
        if(m.type==0)continue;
        processor.setValue(key(i,"freq"),m.freq);
        processor.setValue(key(i,"gain"),m.gain*strength);
        processor.setValue(key(i,"q"),m.q);
        processor.setValue(key(i,"amount"),m.amount*strength);
        processor.setValue(key(i,"ratio"),m.ratio);
        processor.setValue(key(i,"attack"),m.attack);
        processor.setValue(key(i,"release"),m.release);
        processor.setValue(key(i,"mix"),m.mix);
        processor.setValue(key(i,"slope"),m.slope);
    }
    selectSlot(0);
    status.setText("AI chain applied ("+juce::String(arr->size())+" modules). "+o->getProperty("explanation").toString().substring(0,90),juce::dontSendNotification);
}
