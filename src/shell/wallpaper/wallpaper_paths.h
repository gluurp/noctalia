#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct WallpaperConfig;
struct WallpaperLinkGroup;
struct WallpaperMonitorOverride;
struct WaylandOutput;
enum class ThemeMode : std::uint8_t;

namespace wallpaper {

  // One monitor/folder pair resolved for a linked apply. `path` is the target
  // member's directory joined with the applied file's exact relative path.
  // `present` is false when that file does not exist; such targets are reported
  // but never written.
  struct LinkedWallpaperTarget {
    std::string monitor;
    std::string path;
    bool present = false;
  };

  // Result of mapping one applied path into a link group.
  struct LinkedWallpaperMapping {
    const WallpaperLinkGroup* group = nullptr;
    std::string groupId;
    std::string groupName;
    // Member whose directory contained the applied path, and the exact relative
    // path (including extension and subdirectories) under that directory.
    std::string sourceMonitor;
    std::string relativePath;
    std::vector<LinkedWallpaperTarget> targets;
    std::vector<std::string> appliedConnectors;
    std::vector<std::string> missingConnectors;
    // Member whose file drives palette generation, and its resolved path.
    std::string colorSourceMonitor;
    std::string colorSourcePath;
  };

  // Maps a path that lives under a link member's directory to every member of
  // that group using the exact relative path. Returns nullopt when the path is
  // not inside any member directory, so unlinked paths never become linked.
  std::optional<LinkedWallpaperMapping>
  mapLinkedWallpaper(const WallpaperConfig& config, const std::string& appliedPath);

  // Maps theme.mode=auto to the currently resolved light/dark appearance.
  ThemeMode effectiveThemeMode(ThemeMode mode, bool isLight) noexcept;

  const WallpaperMonitorOverride*
  findWallpaperMonitorOverride(const WallpaperConfig& config, const WaylandOutput& output);

  std::string resolveWallpaperDirectory(const WallpaperConfig& config, const WaylandOutput& output, ThemeMode mode);

  std::string resolveGlobalWallpaperDirectory(const WallpaperConfig& config, ThemeMode mode);

} // namespace wallpaper
