#pragma once

#include "core/poll_source.h"

#include <chrono>
#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// Reads the Super (Meta) key from /dev/input/event* on the main loop.
//
// Hyprland drops a bare Super release bind after a scroll or another key
// (issues #6946 / #11483), so the Super+scroll reveal cannot live in the
// compositor binding layer. Reading the kernel evdev stream directly keeps the
// press and release paired. Read-only: the device is never grabbed. No thread,
// no daemon, no IPC round trip: the callback runs on the main thread and drives
// Bar::setSuperHold() directly.
class SuperKeyWatcher final : public PollSource {
public:
  using Callback = std::function<void(bool held)>;

  SuperKeyWatcher() = default;
  ~SuperKeyWatcher() override;

  SuperKeyWatcher(const SuperKeyWatcher&) = delete;
  SuperKeyWatcher& operator=(const SuperKeyWatcher&) = delete;

  void setCallback(Callback callback) { m_callback = std::move(callback); }

  // Opening devices happens here; disabling closes them immediately so an
  // unused feature holds no input fds.
  void setEnabled(bool enabled);
  [[nodiscard]] bool enabled() const noexcept { return m_enabled; }

  int pollTimeoutMs() const override;
  void dispatch(const std::vector<pollfd>& fds, std::size_t startIdx) override;

protected:
  void doAddPollFds(std::vector<pollfd>& fds) override;

private:
  struct Device {
    int fd = -1;
    bool leftDown = false;
    bool rightDown = false;
  };

  void closeDevices();
  void rescan();
  bool openDevice(const std::string& path);
  void readDevice(const std::string& path, Device& device);
  void removeDevice(const std::string& path);
  void recomputeMetaState();
  void emitMetaState();

  Callback m_callback;
  // Open devices, keyed by /dev/input path.
  std::unordered_map<std::string, Device> m_devices;
  // Path order matching the fds appended by the last doAddPollFds(), so
  // dispatch() can map a pollfd index back to its device.
  std::vector<std::string> m_pollOrder;
  bool m_enabled = false;
  bool m_metaDown = false;
  bool m_permissionWarned = false;
  std::chrono::steady_clock::time_point m_lastScan{};
};
