#pragma once

#include <chrono>
#include <complex>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct AudioNode;
class PipeWireService;

class PipeWireSpectrum {
public:
  using ChangeCallback = std::function<void()>;
  using ListenerId = std::uint64_t;

  explicit PipeWireSpectrum(PipeWireService& service);
  ~PipeWireSpectrum();

  PipeWireSpectrum(const PipeWireSpectrum&) = delete;
  PipeWireSpectrum& operator=(const PipeWireSpectrum&) = delete;

  ListenerId addChangeListener(int bandCount, ChangeCallback callback);
  void removeChangeListener(ListenerId id);

  void setTargetNodeId(std::uint32_t id);
  std::uint32_t targetNodeId() const noexcept { return m_targetNodeId; }

  // Capture a specific node instead of the default sink. `name` is a PipeWire
  // node.name (e.g. "easyeffects_sink"); a trailing ".monitor" is ignored, so a
  // pactl-style monitor source name also works. Empty restores the default sink.
  // Sinks are captured through their monitor, sources directly.
  void setTargetNodeName(std::string name);
  [[nodiscard]] const std::string& targetNodeName() const noexcept { return m_targetNodeName; }

  void setLowerCutoff(int freq);
  int lowerCutoff() const noexcept { return m_lowerCutoff; }

  void setUpperCutoff(int freq);
  int upperCutoff() const noexcept { return m_upperCutoff; }

  void setNoiseReduction(float amount);
  float noiseReduction() const noexcept { return m_noiseReduction; }

  void setSmoothing(bool enabled);
  bool smoothing() const noexcept { return m_smoothing; }

  void setFrameRate(int framesPerSecond);

  const std::vector<float>& values(ListenerId id) const noexcept;
  bool idle() const noexcept { return m_idle; }

  int pollTimeoutMs() const;
  void tick();
  void handleAudioStateChanged();

private:
  struct ListenerState {
    int bandCount = 32;
    ChangeCallback callback;
    std::vector<int> analysisBandLow;
    std::vector<int> analysisBandHigh;
    std::vector<float> values;
    std::vector<float> workBands;
    std::vector<float> prevBands;
    std::vector<float> peak;
    std::vector<float> fall;
    std::vector<float> mem;
  };

  class Stream;
  friend class Stream;

  bool hasListeners() const noexcept { return !m_listeners.empty(); }
  void rebuildStream();
  [[nodiscard]] std::uint32_t resolvedTargetNodeId() const noexcept;
  [[nodiscard]] const AudioNode* resolvedTargetNode() const noexcept;
  [[nodiscard]] const AudioNode* findNodeByName(std::string_view name) const noexcept;
  void clearValues(bool notify);
  void emitChanged(ListenerId id);

  void initProcessing();
  // Returns true when every listener was notified because the analysis layout changed.
  bool reconfigureAnalysisLayout();
  void configureListenerState(ListenerState& state, bool resetState);
  void resetListenerState(ListenerState& state, bool clearValues);
  bool processListenerView(ListenerState& state, float nrFactor, double gravityMod);
  std::chrono::nanoseconds frameInterval() const noexcept;
  void computeAnalysisBandBins();
  void feedSamples(const float* monoSamples, int count);
  void processFrame();

  PipeWireService& m_service;
  std::unordered_map<ListenerId, ListenerState> m_listeners;
  ListenerId m_nextListenerId = 1;

  std::uint32_t m_targetNodeId = 0;
  std::string m_targetNodeName;
  std::uint32_t m_boundNodeId = 0;
  std::string m_boundTargetObject;
  int m_analysisBandCount = 32;
  int m_lowerCutoff = 20;
  int m_upperCutoff = 20000;
  float m_noiseReduction = 0.77F;
  bool m_smoothing = true;
  int m_frameRateHz = 60;
  std::vector<float> m_analysisBands;
  bool m_idle = true;

  std::unique_ptr<Stream> m_stream;
  std::chrono::steady_clock::time_point m_nextFrameAt;

  static constexpr int kFftSize = 4096;
  std::vector<float> m_ringBuffer;
  int m_ringPos = 0;
  bool m_ringFull = false;
  int m_sampleRate = 48000;
  int m_idleFrames = 0;
  bool m_samplesReceived = false;

  // Env-gated diagnostics (NOCTALIA_SPECTRUM_DEBUG=1): ~1 line/s capture + analysis summaries.
  void noteProcessDiag(int frameCount, int channelCount, float peak);
  bool m_diagEnabled = false;
  std::chrono::steady_clock::time_point m_diagLastProcessLog;
  std::chrono::steady_clock::time_point m_diagLastFrameLog;
  float m_diagPeak = 0.0F;
  int m_diagBatches = 0;
  int m_diagFrames = 0;

  std::vector<float> m_window;
  std::vector<float> m_analysisBandBins;
  std::vector<int> m_analysisBandLowBins;
  std::vector<int> m_analysisBandHighBins;
  float m_sensitivity = 1.0F;
  bool m_sensInit = true;
  std::vector<std::complex<float>> m_fftBuf;
};
