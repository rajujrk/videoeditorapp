// A VST3 effect for the tests: a gain (0..1 → silence to twice as loud, 0.5 unchanged) whose
// output lags its input by 64 samples (and says so), with its gain in its state. No editor.

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/vsttypes.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "base/source/fstreamer.h"

#include <array>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

constexpr ParamID kGain = 1;
constexpr int kLatency = 64;

class TestGain : public SingleComponentEffect {
public:
    static FUnknown* create(void*) { return static_cast<IAudioProcessor*>(new TestGain); }

    tresult PLUGIN_API initialize(FUnknown* context) override
    {
        if (SingleComponentEffect::initialize(context) != kResultOk)
            return kResultFalse;
        addAudioInput(STR16("In"), SpeakerArr::kStereo);
        addAudioOutput(STR16("Out"), SpeakerArr::kStereo);
        parameters.addParameter(STR16("Gain"), STR16("x"), 0, 0.5, ParameterInfo::kCanAutomate, kGain);
        return kResultOk;
    }

    tresult PLUGIN_API setBusArrangements(SpeakerArrangement* inputs, int32 numIns, SpeakerArrangement* outputs,
                                          int32 numOuts) override
    {
        if (numIns == 1 && numOuts == 1 && inputs[0] == SpeakerArr::kStereo && outputs[0] == SpeakerArr::kStereo)
            return SingleComponentEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
        return kResultFalse;
    }
    tresult PLUGIN_API canProcessSampleSize(int32 size) override { return size == kSample32 ? kResultTrue : kResultFalse; }
    uint32 PLUGIN_API getLatencySamples() override { return kLatency; }

    tresult PLUGIN_API setActive(TBool state) override
    {
        for (auto& line : m_lines)
            line.assign(kLatency, 0.0f);
        m_at = 0;
        return SingleComponentEffect::setActive(state);
    }

    tresult PLUGIN_API process(ProcessData& data) override
    {
        if (data.inputParameterChanges) {
            for (int32 i = 0; i < data.inputParameterChanges->getParameterCount(); ++i) {
                IParamValueQueue* queue = data.inputParameterChanges->getParameterData(i);
                ParamValue value = 0;
                int32 offset = 0;
                if (queue && queue->getParameterId() == kGain && queue->getPointCount() > 0
                    && queue->getPoint(queue->getPointCount() - 1, offset, value) == kResultOk) {
                    m_gain = value;
                    setParamNormalized(kGain, value);
                }
            }
        }
        if (data.numInputs < 1 || data.numOutputs < 1 || data.numSamples <= 0)
            return kResultOk;
        const float gain = float(m_gain * 2.0);
        for (int c = 0; c < 2; ++c) {
            float* in = data.inputs[0].channelBuffers32[c];
            float* out = data.outputs[0].channelBuffers32[c];
            int at = m_at;
            for (int32 i = 0; i < data.numSamples; ++i) {
                const float delayed = m_lines[std::size_t(c)][std::size_t(at)];
                m_lines[std::size_t(c)][std::size_t(at)] = in[i] * gain;
                out[i] = delayed;
                at = (at + 1) % kLatency;
            }
            if (c == 1)
                m_at = at;
        }
        return kResultOk;
    }

    tresult PLUGIN_API setState(IBStream* state) override
    {
        IBStreamer streamer(state, kLittleEndian);
        double gain = 0;
        if (!streamer.readDouble(gain))
            return kResultFalse;
        m_gain = gain;
        setParamNormalized(kGain, gain);
        return kResultOk;
    }
    tresult PLUGIN_API getState(IBStream* state) override
    {
        IBStreamer streamer(state, kLittleEndian);
        return streamer.writeDouble(m_gain) ? kResultOk : kResultFalse;
    }
    tresult PLUGIN_API setParamNormalized(ParamID tag, ParamValue value) override
    {
        if (tag == kGain)
            m_gain = value;
        return SingleComponentEffect::setParamNormalized(tag, value);
    }

private:
    double m_gain = 0.5;
    std::array<std::vector<float>, 2> m_lines{std::vector<float>(kLatency), std::vector<float>(kLatency)};
    int m_at = 0;
};

} // namespace

BEGIN_FACTORY_DEF("TechieVideoEditor tests", "https://example.invalid", "mailto:tests@example.invalid")
DEF_CLASS2(INLINE_UID(0x5A3C1E42, 0x7B9D4F60, 0x8E21C3A4, 0xD05B6F17), PClassInfo::kManyInstances, kVstAudioEffectClass,
           "TVE Test Gain", Vst::kDistributable, "Fx", "1.0.0", kVstVersionString, TestGain::create)
END_FACTORY

// Called by the SDK's bundle entry points; nothing to set up.
bool InitModule()
{
    return true;
}
bool DeinitModule()
{
    return true;
}
