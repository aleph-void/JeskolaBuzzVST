#include "TestFramework.h"
#include "vst3/BuzzProcessor.h"
#include "vst3/BuzzController.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include <algorithm>

using namespace BuzzVst;
using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {
// Exercise the real processor block scheduling and conversion with deterministic
// machine audio, without depending on a third-party DLL or an audio device.
class TestProcessor : public BuzzProcessor {
public:
	void prepare(double rate, bool effect) {
		ProcessSetup setup{};
		setup.sampleRate = rate;
		setup.maxSamplesPerBlock = 65536;
		setup.symbolicSampleSize = kSample32;
		setupProcessing(setup);
		machineType = effect ? MT_EFFECT : MT_GENERATOR;
		machineReady = true;
	}
	void useMissingBridge() {
#ifdef BUZZVST_64BIT
		bridgeHostPath = "Z:\\BuzzBridgeMissingTestDirectory\\MissingHost.exe";
#endif
	}
	int loadRequests = 0;
	tresult PLUGIN_API notify(IMessage* message) override {
		if (strcmp(message->getMessageID(), "BuzzDllPath") == 0) ++loadRequests;
		return BuzzProcessor::notify(message);
	}
protected:
	void setupBuses() override {}
	bool acceptsMachineType(int) const override { return true; }
	void processAudioBlock(float** inputs, float** outputs, int32, int32, int32 count) override {
		for (int i = 0; i < count; ++i, ++sourcePosition) {
			for (int ch = 0; ch < 2; ++ch) {
				outputs[ch][i] = (inputs && inputs[ch]) ? inputs[ch][i] :
					(float)sin(sourcePosition * (ch + 1) * 0.142);
			}
		}
	}
private:
	int sourcePosition = 0;
};

class TestController : public EffectController {
public:
	TestController() { gearDirPath = "unused-test-gear-directory"; }
	const std::string& name() const { return currentMachineName; }
	const std::string& path() const { return currentDllPath; }
	int failures = 0;
	tresult PLUGIN_API notify(IMessage* message) override {
		if (strcmp(message->getMessageID(), "BuzzMachineLoadFailed") == 0) ++failures;
		return BuzzController::notify(message);
	}
};

std::vector<float> render(double rate, bool effect, const std::vector<int>& blocks) {
	TestProcessor processor;
	processor.prepare(rate, effect);
	constexpr int total = 32768;
	std::vector<float> inL(total), inR(total), outL(total), outR(total);
	for (int i = 0; i < total; ++i) {
		inL[i] = (float)sin(i * 0.142);
		inR[i] = (float)cos(i * 0.217);
	}
	int offset = 0;
	int block = 0;
	while (offset < total) {
		int count = std::min(blocks[block++ % blocks.size()], total - offset);
		float* inputs[] = {inL.data() + offset, inR.data() + offset};
		float* outputs[] = {outL.data() + offset, outR.data() + offset};
		AudioBusBuffers input{}, output{};
		input.numChannels = output.numChannels = 2;
		input.channelBuffers32 = inputs;
		output.channelBuffers32 = outputs;
		ProcessData data{};
		data.numSamples = count;
		data.numInputs = effect ? 1 : 0;
		data.numOutputs = 1;
		data.inputs = &input;
		data.outputs = &output;
		processor.process(data);
		offset += count;
	}
	outL.insert(outL.end(), outR.begin(), outR.end());
	return outL;
}

void checkBlockContinuity(bool effect) {
	for (double rate : {22050.0, 44100.0, 48000.0, 88200.0, 96000.0, 192000.0}) {
		auto reference = render(rate, effect, {32768});
		ASSERT_GT(*std::max_element(reference.begin(), reference.end()), 0.25f);
		for (const auto& blocks : {std::vector<int>{128}, std::vector<int>{512},
		                          std::vector<int>{1, 7, 37, 128, 511, 64}}) {
			auto actual = render(rate, effect, blocks);
			for (size_t i = 0; i < reference.size(); ++i) {
				ASSERT_TRUE(std::isfinite(actual[i]));
				ASSERT_NEAR(actual[i], reference[i], 0.000001);
			}
		}
	}
}
}

TEST(ProcessorRegression, GeneratorBlockContinuity) {
	checkBlockContinuity(false);
}

TEST(ProcessorRegression, EffectBlockContinuity) {
	checkBlockContinuity(true);
}

TEST(ProcessorRegression, FailedLoadRepliesWithoutAudioAndCanRetry) {
	HostApplication host;
	TestProcessor processor;
	TestController controller;
	processor.initialize(&host);
	controller.initialize(&host);
	processor.useMissingBridge();
	processor.connect(&controller);
	controller.connect(&processor);

	// No editor, process(), or status polling: a stopped DAW must still get
	// a terminal response, and selecting the same failed DLL must retry.
	controller.onDllPathSelected("Z:\\BuzzBridgeMissingTestDirectory\\MissingMachine.dll");
	CHECK_EQ(controller.failures, 1);
	CHECK_EQ(controller.name(), std::string("Load failed"));
	CHECK_TRUE(controller.path().empty());
	controller.onDllPathSelected("Z:\\BuzzBridgeMissingTestDirectory\\MissingMachine.dll");
	CHECK_EQ(controller.failures, 2);
	CHECK_EQ(processor.loadRequests, 2);

	controller.disconnect(&processor);
	processor.disconnect(&controller);
	controller.terminate();
	processor.terminate();
}

TEST(ProcessorRegression, FailureReasonPersistsWithoutEditor) {
	TestController controller;
	HostMessage message;
	message.setMessageID("BuzzMachineLoadFailed");
	const std::string reason = "This machine needs the Generator plugin.";
	message.getAttributes()->setBinary("Reason", reason.data(), (uint32)reason.size());
	controller.notify(&message);
	ASSERT_EQ(controller.name(), reason);
}

TEST(ProcessorRegression, BufferedResamplerResetDiscardsPreviousInput) {
	BuzzResampler resampler;
	resampler.init(48000, 44100);
	float input[128];
	std::fill(std::begin(input), std::end(input), 1.0f);
	resampler.processBuffered(input, 128, nullptr, 0);
	resampler.reset();
	std::fill(std::begin(input), std::end(input), 0.0f);
	float output[100];
	resampler.processBuffered(input, 128, output, 100);
	for (float value : output) ASSERT_EQ(value, 0.0f);
	std::fill(std::begin(input), std::end(input), 1.0f);
	resampler.processBuffered(input, 128, nullptr, 0);
	resampler.init(96000, 44100);
	std::fill(std::begin(input), std::end(input), 0.0f);
	resampler.processBuffered(input, 128, output, 50);
	for (int i = 0; i < 50; ++i) ASSERT_EQ(output[i], 0.0f);
}
