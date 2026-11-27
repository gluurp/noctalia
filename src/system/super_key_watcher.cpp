#include "system/super_key_watcher.h"

#include "core/log.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <linux/input.h>
#include <linux/input-event-codes.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <utility>

namespace {
  constexpr Logger kLog("super-watch");
  constexpr int kSuperLeft = KEY_LEFTMETA;
  constexpr int kSuperRight = KEY_RIGHTMETA;
  // Wake even when no key event arrives, so keyboards plugged in later are
  // picked up and vanished devices are dropped. Reads are non-blocking.
  constexpr int kRescanIntervalMs = 2000;
  constexpr std::size_t kMaxEventsPerDispatch = 64;
  constexpr std::size_t kBitsPerLong = 8 * sizeof(unsigned long);

  bool keyBitSet(const unsigned long* bits, int code) {
    const auto index = static_cast<std::size_t>(code);
    return (bits[index / kBitsPerLong] & (1UL << (index % kBitsPerLong))) != 0;
  }

  bool deviceHasSuperKeys(int fd) {
    unsigned long keyBits[(KEY_MAX + kBitsPerLong) / kBitsPerLong] = {};
    if (::ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keyBits)), keyBits) < 0) {
      return false;
    }
    return keyBitSet(keyBits, kSuperLeft) || keyBitSet(keyBits, kSuperRight);
  }

  std::vector<std::string> listEventDevices() {
    std::vector<std::string> paths;
    std::error_code ec;
    for (std::filesystem::directory_iterator it("/dev/input", ec), end; !ec && it != end; it.increment(ec)) {
      const std::string name = it->path().filename().string();
      if (name.rfind("event", 0) == 0) {
        paths.push_back(it->path().string());
      }
    }
    std::ranges::sort(paths);
    return paths;
  }
} // namespace

SuperKeyWatcher::~SuperKeyWatcher() { closeDevices(); }

void SuperKeyWatcher::setEnabled(bool enabled) {
  if (enabled == m_enabled) {
    return;
  }
  m_enabled = enabled;
  if (enabled) {
    m_permissionWarned = false;
    m_metaDown = false;
    rescan();
    kLog.info("enabled ({} device(s))", m_devices.size());
  } else {
    const bool wasDown = m_metaDown;
    closeDevices();
    m_metaDown = false;
    if (wasDown && m_callback) {
      m_callback(false);
    }
    kLog.info("disabled");
  }
}

int SuperKeyWatcher::pollTimeoutMs() const { return m_enabled ? kRescanIntervalMs : -1; }

void SuperKeyWatcher::doAddPollFds(std::vector<pollfd>& fds) {
  m_pollOrder.clear();
  if (!m_enabled) {
    return;
  }
  m_pollOrder.reserve(m_devices.size());
  for (const auto& [path, device] : m_devices) {
    if (device.fd < 0) {
      continue;
    }
    fds.push_back({.fd = device.fd, .events = POLLIN, .revents = 0});
    m_pollOrder.push_back(path);
  }
}

void SuperKeyWatcher::dispatch(const std::vector<pollfd>& fds, std::size_t startIdx) {
  if (!m_enabled) {
    return;
  }
  const std::size_t available = fds.size() > startIdx ? fds.size() - startIdx : 0;
  const std::size_t count = std::min(available, m_pollOrder.size());

  // Snapshot the ready devices: a read can drop a device from the map and
  // invalidate iterators.
  std::vector<std::pair<std::string, short>> ready;
  ready.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    const short revents = fds[startIdx + i].revents;
    if (revents != 0) {
      ready.emplace_back(m_pollOrder[i], revents);
    }
  }

  for (const auto& [path, revents] : ready) {
    const auto it = m_devices.find(path);
    if (it == m_devices.end()) {
      continue;
    }
    if ((revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
      removeDevice(path);
      continue;
    }
    if ((revents & POLLIN) != 0) {
      readDevice(path, it->second);
    }
  }

  const auto now = std::chrono::steady_clock::now();
  if (now - m_lastScan >= std::chrono::milliseconds(kRescanIntervalMs)) {
    rescan();
  }
}

void SuperKeyWatcher::closeDevices() {
  for (auto& [path, device] : m_devices) {
    if (device.fd >= 0) {
      ::close(device.fd);
      device.fd = -1;
    }
  }
  m_devices.clear();
  m_pollOrder.clear();
  m_metaDown = false;
}

void SuperKeyWatcher::rescan() {
  const std::vector<std::string> paths = listEventDevices();
  const auto now = std::chrono::steady_clock::now();
  m_lastScan = now;

  for (auto it = m_devices.begin(); it != m_devices.end();) {
    if (std::ranges::find(paths, it->first) == paths.end()) {
      if (it->second.fd >= 0) {
        ::close(it->second.fd);
      }
      it = m_devices.erase(it);
      kLog.debug("device gone");
    } else {
      ++it;
    }
  }

  for (const std::string& path : paths) {
    if (!m_devices.contains(path)) {
      openDevice(path);
    }
  }

  recomputeMetaState();
}

bool SuperKeyWatcher::openDevice(const std::string& path) {
  const int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
  if (fd < 0) {
    if ((errno == EACCES || errno == EPERM) && !m_permissionWarned) {
      m_permissionWarned = true;
      kLog.warn(
          "cannot open {}: {} (Super reveal stays inactive; add the user to the input group)",
          path, std::strerror(errno)
      );
    } else if (errno != ENOENT && errno != EACCES && errno != EPERM) {
      kLog.debug("open {}: {}", path, std::strerror(errno));
    }
    return false;
  }

  if (!deviceHasSuperKeys(fd)) {
    ::close(fd);
    return false;
  }

  m_devices.emplace(path, Device{.fd = fd});
  kLog.debug("watching {}", path);
  return true;
}

void SuperKeyWatcher::readDevice(const std::string& path, Device& device) {
  input_event events[32];
  std::size_t processed = 0;
  while (processed < kMaxEventsPerDispatch) {
    const ssize_t bytes = ::read(device.fd, events, sizeof(events));
    if (bytes < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break;
      }
      if (errno == ENODEV || errno == EIO) {
        removeDevice(path);
        return;
      }
      kLog.debug("read {}: {}", path, std::strerror(errno));
      break;
    }
    if (bytes == 0) {
      removeDevice(path);
      return;
    }

    const auto count = static_cast<std::size_t>(bytes) / sizeof(input_event);
    bool stateChanged = false;
    for (std::size_t i = 0; i < count; ++i) {
      const input_event& event = events[i];
      if (event.type != EV_KEY || (event.code != kSuperLeft && event.code != kSuperRight)) {
        continue;
      }
      if (event.value == 2) {
        continue; // autorepeat
      }
      bool& down = event.code == kSuperLeft ? device.leftDown : device.rightDown;
      const bool next = event.value != 0;
      if (down == next) {
        continue;
      }
      down = next;
      stateChanged = true;
    }
    processed += count;

    if (stateChanged) {
      recomputeMetaState();
    }
  }
}

void SuperKeyWatcher::removeDevice(const std::string& path) {
  const auto it = m_devices.find(path);
  if (it == m_devices.end()) {
    return;
  }
  if (it->second.fd >= 0) {
    ::close(it->second.fd);
  }
  m_devices.erase(it);
  kLog.debug("device removed");
  recomputeMetaState();
}

void SuperKeyWatcher::recomputeMetaState() {
  bool anyDown = false;
  for (const auto& [path, device] : m_devices) {
    anyDown = anyDown || device.leftDown || device.rightDown;
  }
  if (anyDown == m_metaDown) {
    return;
  }
  m_metaDown = anyDown;
  emitMetaState();
}

void SuperKeyWatcher::emitMetaState() {
  if (m_metaDown) {
    kLog.info("press");
  } else {
    kLog.info("release");
  }
  if (m_callback) {
    m_callback(m_metaDown);
  }
}
