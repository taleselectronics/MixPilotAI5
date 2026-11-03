#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <cmath>

namespace {
float db(float x){return juce::Decibels::gainToDecibels(juce::jmax(x,1.e-9f));}
float amp(float x){return juce::Decibels::decibelsToGain(x);}
const float diagnosticHz[]{130,250,460,850,1550,2850,4600,7100,10700};
const char* slotKeys[]{"type","enable","freq","gain","q","amount","ratio","attack","release","mix","slope"};
juce::String id(int i,const char* key){return "s"+juce::String(i+1)+key;}
}
juce::StringArray MixPilotProcessor::typeNames(){return {"Empty","Parametric EQ","Low Cut / HPF","High Cut / LPF","Compressor","De-esser","Saturation","Gain / Trim","Stereo Width","Echo Delay","Soft Limiter"};}
MixPilotProcessor::MixPilotProcessor()
 : AudioProcessor(BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true)
                                   .withOutput("Output",juce::AudioChannelSet::stereo(),true)),
   apvts(*this,nullptr,"PARAMETERS",makeLayout())
{
    for(auto& p:ring)p.store(0);
    for(auto& p:measured)p.store(-90);
    for(auto& p:fileMeasured)p.store(-90);
}
juce::AudioProcessorValueTreeState::ParameterLayout MixPilotProcessor::makeLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    auto add=[&](const String& key,const String& label,float lo,float hi,float initial,float step=0.01f){
        p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{key,1},label,NormalisableRange<float>(lo,hi,step),initial));
    };
    auto choice=[&](const String& key,const String& label,const StringArray& options,int initial){
        p.push_back(std::make_unique<AudioParameterChoice>(ParameterID{key,1},label,options,initial));
    };
    choice("mode","Source",{"Universal","Vocals","Instrument","Drums","Stereo Mix"},1);
    choice("mic","Microphone",{"Unknown / Flat","Neumann TLM 103","Generic dynamic microphone"},1);
    add("output","Master output dB",-24,12,0);
    add("wet","Global wet percentage",0,100,100);
    add("intensity","AI strength percentage",0,100,80);
    for(int i=0;i<slots;++i){
        const int defaults[]={2,1,4,1,5,6,3,0,0,0};
        choice(id(i,"type"),"Module "+String(i+1)+" type",typeNames(),defaults[i]);
        p.push_back(std::make_unique<AudioParameterBool>(ParameterID{id(i,"enable"),1},"Module enabled",true));
        add(id(i,"freq"),"Module frequency Hz",20,20000,i==0?75:i==6?16000:1200,1);
        add(id(i,"gain"),"Module EQ/gain dB",-18,18,0);
        add(id(i,"q"),"EQ Q",0.3f,12,1.1f);
        add(id(i,"amount"),"Processing amount",0,100,i==2?42:30);
        add(id(i,"ratio"),"Compressor ratio",1,12,3);
        add(id(i,"attack"),"Attack ms",0.1f,200,12);
        add(id(i,"release"),"Release ms",10,1000,115);
        add(id(i,"mix"),"Module wet",0,100,100);
        choice(id(i,"slope"),"Cut slope",{"12 dB/oct","24 dB/oct"},0);
    }
    return {p.begin(),p.end()};
}
bool MixPilotProcessor::isBusesLayoutSupported(const BusesLayout& l) const {
    auto input=l.getMainInputChannelSet();
    return (input==juce::AudioChannelSet::mono() || input==juce::AudioChannelSet::stereo()) && input==l.getMainOutputChannelSet();
}
float MixPilotProcessor::value(const juce::String& key) const {auto* p=apvts.getRawParameterValue(key);return p?p->load():0;}
void MixPilotProcessor::setValue(const juce::String& key,float v){
    if(auto* p=apvts.getParameter(key)){
        auto range=p->getNormalisableRange();
        p->setValueNotifyingHost(range.convertTo0to1(juce::jlimit(range.start,range.end,v)));
    }
}
void MixPilotProcessor::setSlotType(int s,int type){if(s>=0&&s<slots)setValue(id(s,"type"),(float)juce::jlimit(0,types-1,type));}
void MixPilotProcessor::swapSlots(int a,int b){
    if(a<0||b<0||a>=slots||b>=slots||a==b)return;
    std::array<float,11> av{},bv{};
    for(int k=0;k<11;++k){av[(size_t)k]=value(id(a,slotKeys[k]));bv[(size_t)k]=value(id(b,slotKeys[k]));}
    for(int k=0;k<11;++k){setValue(id(a,slotKeys[k]),bv[(size_t)k]);setValue(id(b,slotKeys[k]),av[(size_t)k]);}
}
void MixPilotProcessor::prepareToPlay(double rate,int){
    sampleRate=juce::jmax(8000.0,rate);
    targetFrames=(int64_t)(sampleRate*6.0);
    capturing=false;frames=0;captured.store(false);
    for(auto& ch:stage)for(auto& s:ch){s.filter.reset();s.second.reset();s.envelope=0;s.compGain=1;s.sibilanceLp=0;s.delayBuffer.assign((size_t)(sampleRate*1.01),0.0f);s.delayPosition=0;}
    for(auto& ch:detector)for(auto& f:ch)f.reset();
    for(auto& s:lowState)s=0; for(auto& s:highState)s=0;
    updateFilters(); filterCountdown=0;
}
void MixPilotProcessor::updateFilters(){
    for(int i=0;i<slots;++i){
        const int type=(int)value(id(i,"type"));
        const double hz=juce::jlimit(20.0,sampleRate*.44,(double)value(id(i,"freq")));
        const double q=juce::jlimit(.3,12.0,(double)value(id(i,"q")));
        juce::IIRCoefficients co;
        if(type==2)co=juce::IIRCoefficients::makeHighPass(sampleRate,hz);
        else if(type==3)co=juce::IIRCoefficients::makeLowPass(sampleRate,hz);
        else co=juce::IIRCoefficients::makePeakFilter(sampleRate,hz,q,amp(value(id(i,"gain"))));
        for(auto& channel:stage){channel[(size_t)i].filter.setCoefficients(co);channel[(size_t)i].second.setCoefficients(co);}
    }
    for(int i=0;i<diagnosticBands;++i){
        auto co=juce::IIRCoefficients::makeBandPass(sampleRate,juce::jmin((double)diagnosticHz[i],sampleRate*.44),2.3);
        for(auto& ch:detector)ch[(size_t)i].setCoefficients(co);
    }
}
void MixPilotProcessor::startAnalysis() noexcept {fileValid.store(false);captured.store(false);analysisRequested.store(true);}
void MixPilotProcessor::processBlock(juce::AudioBuffer<float>& b,juce::MidiBuffer&){
    juce::ScopedNoDenormals nd;
    const int count=b.getNumSamples(), chans=juce::jmin(2,b.getNumChannels());
    if(count<=0||chans<1)return;
    if(analysisRequested.exchange(false)){
        capturing=true;captured.store(false);frames=0;rmsSum=lowSum=midSum=highSum=0;peak=0;
        diagnosticSum.fill(0);
        for(auto& ch:detector)for(auto& f:ch)f.reset();
    }
    if(filterCountdown<=0){updateFilters();filterCountdown=(int)(sampleRate*.07);}
    filterCountdown-=count;
    float in=0,out=0,gr=0;
    const float lowCoef=std::exp(-2.0f*juce::MathConstants<float>::pi*210.0f/(float)sampleRate);
    const float hiCoef=std::exp(-2.0f*juce::MathConstants<float>::pi*4700.0f/(float)sampleRate);
    for(int n=0;n<count;++n){
        float dry[2]{},y[2]{};
        float mono=0;
        for(int c=0;c<chans;++c){
            dry[c]=b.getReadPointer(c)[n];y[c]=dry[c];
            in=juce::jmax(in,std::abs(dry[c]));mono+=dry[c]/chans;
            if(capturing){
                const float x=dry[c];
                lowState[c]=lowCoef*lowState[c]+(1-lowCoef)*x;
                highState[c]=hiCoef*highState[c]+(1-hiCoef)*x;
                const float lo=lowState[c],hi=x-highState[c],mid=x-lo-hi;
                rmsSum+=x*x;lowSum+=lo*lo;midSum+=mid*mid;highSum+=hi*hi;
                peak=juce::jmax(peak,std::abs(x));
                for(int k=0;k<diagnosticBands;++k){const float d=detector[(size_t)c][(size_t)k].processSingleSampleRaw(x);diagnosticSum[(size_t)k]+=d*d;}
            }
        }
        const auto w=ringWrite.fetch_add(1,std::memory_order_relaxed);
        ring[(size_t)(w%spectrumSize)].store(mono,std::memory_order_relaxed);
        for(int i=0;i<slots;++i){
            const int type=(int)value(id(i,"type"));
            if(type==0||value(id(i,"enable"))<.5f)continue;
            const float amount=value(id(i,"amount"))*.01f;
            const float mix=value(id(i,"mix"))*.01f;
            float stageInput[2]{y[0],y[1]};
            for(int c=0;c<chans;++c){
                auto& st=stage[(size_t)c][(size_t)i];
                float x=y[c];
                switch(type){
                    case 1:case 2:case 3:
                        x=st.filter.processSingleSampleRaw(x);
                        if((type==2||type==3)&&(int)value(id(i,"slope"))==1)x=st.second.processSingleSampleRaw(x);
                        break;
                    case 4:{
                        const float attackMs=value(id(i,"attack"));
                        const float releaseMs=value(id(i,"release"));
                        const float atk=std::exp(-1.0f/(float)(sampleRate*attackMs*.001));
                        const float rel=std::exp(-1.0f/(float)(sampleRate*releaseMs*.001));
                        const float power=std::abs(x);
                        const float cf=power>st.envelope?atk:rel;
                        st.envelope=cf*st.envelope+(1-cf)*power;
                        const float threshold=-6-42*amount;
                        const float ratio=value(id(i,"ratio"));
                        const float over=juce::jmax(0.0f,db(st.envelope)-threshold);
                        const float target=amp(-over*(1-1/ratio));
                        st.compGain+=(target-st.compGain)*.008f;
                        x*=st.compGain;gr=juce::jmax(gr,-db(st.compGain));
                        break;
                    }
                    case 5:{ // lightweight broadband de-esser; upgrade to split-band in future
                        const float a=std::exp(-2.0f*juce::MathConstants<float>::pi*juce::jlimit(2000.0f,12000.0f,value(id(i,"freq")))/(float)sampleRate);
                        st.sibilanceLp=a*st.sibilanceLp+(1-a)*x;
                        const float hi=std::abs(x-st.sibilanceLp);
                        st.envelope=.98f*st.envelope+.02f*hi;
                        const float cut=juce::jlimit(0.0f,12.0f,(db(st.envelope)+34)*amount*.5f);
                        x*=amp(-cut);gr=juce::jmax(gr,cut);
                        break;
                    }
                    case 6:{const float drive=1+amount*12;x=std::tanh(x*drive)/std::tanh(drive);break;}
                    case 7:x*=amp(value(id(i,"gain")));break;
                    case 9:{
                        const size_t delaySamples=(size_t)juce::jlimit(10.0,sampleRate*.999,(double)(sampleRate*value(id(i,"release"))*.001));
                        auto& buffer=st.delayBuffer;
                        if(!buffer.empty()){
                            const size_t pos=st.delayPosition;
                            const size_t read=(pos+buffer.size()-delaySamples)%buffer.size();
                            const float delayed=buffer[read];
                            buffer[pos]=x+delayed*(.82f*amount);
                            st.delayPosition=(pos+1)%buffer.size();
                            x=delayed;
                        }
                        break;
                    }
                    case 10:{
                        // Character soft-limiter, NOT a true-peak brickwall limiter.
                        const float drive=1.0f+10.0f*amount;
                        x=std::tanh(x*drive)/drive;
                        break;
                    }
                    default:break;
                }
                y[c]=stageInput[c]*(1-mix)+x*mix;
            }
            if(type==8&&chans==2){
                const float width=juce::jmap(amount,0.0f,2.0f); // 0: mono, 50: unchanged, 100: expanded
                const float mid=.5f*(stageInput[0]+stageInput[1]);
                const float side=.5f*(stageInput[0]-stageInput[1])*width;
                y[0]=stageInput[0]*(1-mix)+(mid+side)*mix;
                y[1]=stageInput[1]*(1-mix)+(mid-side)*mix;
            }
        }
        const float wet=value("wet")*.01f,output=amp(value("output"));
        for(int c=0;c<chans;++c){
            const float result=(dry[c]*(1-wet)+y[c]*wet)*output;
            // Safety peak clamp, not true-peak limiting.
            b.getWritePointer(c)[n]=juce::jlimit(-1.0f,1.0f,result);
            out=juce::jmax(out,std::abs(b.getReadPointer(c)[n]));
        }
    }
    inLevel.store(in);outLevel.store(out);reduction.store(gr);
    if(capturing){
        frames+=count;
        if(frames>=targetFrames){
            const double samples=(double)juce::jmax<int64_t>(1,frames*chans);
            auto fromEnergy=[&](double e){return db((float)std::sqrt(e/samples));};
            measured[0].store(fromEnergy(rmsSum));measured[1].store(fromEnergy(lowSum));
            measured[2].store(fromEnergy(midSum));measured[3].store(fromEnergy(highSum));
            measured[4].store(db(peak));
            std::array<float,diagnosticBands> bands{};
            for(int k=0;k<diagnosticBands;++k)bands[(size_t)k]=fromEnergy(diagnosticSum[(size_t)k]);
            float best=0,hz=0;
            for(int k=1;k<diagnosticBands-1;++k){
                const float prominence=bands[(size_t)k]-.5f*(bands[(size_t)(k-1)]+bands[(size_t)(k+1)]);
                if(prominence>best&&bands[(size_t)k]>-70){best=prominence;hz=diagnosticHz[k];}
            }
            resonanceHz.store(best>=2?hz:0);resonanceDb.store(best);
            capturing=false;captured.store(true);
        }
    }
}
void MixPilotProcessor::setFileFeatures(ToneFeatures f) noexcept {
    fileValid.store(false);
    fileMeasured[0].store(f.rmsDb);fileMeasured[1].store(f.lowDb);fileMeasured[2].store(f.midDb);
    fileMeasured[3].store(f.highDb);fileMeasured[4].store(f.peakDb);fileValid.store(f.valid);
}
ToneFeatures MixPilotProcessor::features() const noexcept {
    ToneFeatures f;
    if(!fileValid.load()&&!captured.load())return f;
    auto& source=fileValid.load()?fileMeasured:measured;
    f.rmsDb=source[0].load();f.lowDb=source[1].load();f.midDb=source[2].load();
    f.highDb=source[3].load();f.peakDb=source[4].load();f.valid=true;return f;
}
void MixPilotProcessor::spectrum(std::array<float,spectrumSize>& out) const noexcept {
    const unsigned int end=ringWrite.load(std::memory_order_relaxed);
    for(int k=0;k<spectrumSize;++k)out[(size_t)k]=ring[(size_t)((end+k)%spectrumSize)].load(std::memory_order_relaxed);
}
void MixPilotProcessor::setPrompt(juce::String s){juce::ScopedLock l(stateLock);promptText=std::move(s);}
juce::String MixPilotProcessor::prompt() const {juce::ScopedLock l(stateLock);return promptText;}
void MixPilotProcessor::setReference(ToneFeatures f){juce::ScopedLock l(stateLock);referenceFeatures=f;}
ToneFeatures MixPilotProcessor::reference() const {juce::ScopedLock l(stateLock);return referenceFeatures;}
void MixPilotProcessor::getStateInformation(juce::MemoryBlock& out){
    auto s=apvts.copyState();
    {juce::ScopedLock l(stateLock);
        s.setProperty("prompt",promptText,nullptr);
        s.setProperty("referenceValid",referenceFeatures.valid,nullptr);
        s.setProperty("refRms",referenceFeatures.rmsDb,nullptr);
        s.setProperty("refLow",referenceFeatures.lowDb,nullptr);
        s.setProperty("refMid",referenceFeatures.midDb,nullptr);
        s.setProperty("refHigh",referenceFeatures.highDb,nullptr);
        s.setProperty("refPeak",referenceFeatures.peakDb,nullptr);
    }
    if(auto xml=s.createXml())copyXmlToBinary(*xml,out);
}
void MixPilotProcessor::setStateInformation(const void* data,int length){
    if(auto xml=getXmlFromBinary(data,length)){
        auto s=juce::ValueTree::fromXml(*xml);
        if(s.isValid()&&s.hasType(apvts.state.getType())){
            {juce::ScopedLock l(stateLock);
                promptText=s.getProperty("prompt","").toString();
                referenceFeatures.valid=(bool)s.getProperty("referenceValid",false);
                referenceFeatures.rmsDb=(float)s.getProperty("refRms",-90.0f);
                referenceFeatures.lowDb=(float)s.getProperty("refLow",-90.0f);
                referenceFeatures.midDb=(float)s.getProperty("refMid",-90.0f);
                referenceFeatures.highDb=(float)s.getProperty("refHigh",-90.0f);
                referenceFeatures.peakDb=(float)s.getProperty("refPeak",-90.0f);
            }
            apvts.replaceState(s);filterCountdown=0;
        }
    }
}
juce::AudioProcessorEditor* MixPilotProcessor::createEditor(){return new MixPilotEditor(*this);}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new MixPilotProcessor();}
