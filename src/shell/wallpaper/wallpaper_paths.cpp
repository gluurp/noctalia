#include "shell/wallpaper/wallpaper_paths.h"

#include "config/config_types.h"
#include "util/file_utils.h"

#include <algorithm>
#include <filesystem>
#include <system_error>

namespace {

  std::filesystem::path expandedPath(std::string_view raw) {
    return FileUtils::resolvePath(FileUtils::expandEnvVars(std::string(raw)));
  }

  // `path` relative to `directory`, or nullopt when it is not strictly inside.
  // Purely lexical, so it works for missing and not-yet-existing targets and
  // never walks through `..`.
  std::optional<std::filesystem::path>
  relativePathInside(const std::filesystem::path& path, const std::filesystem::path& directory) {
    if (directory.empty()) {
      return std::nullopt;
    }
    const auto validRelative = [](const std::filesystem::path& relative) -> std::optional<std::filesystem::path> {
      if (relative.empty()) {
        return std::nullopt;
      }
      const auto first = relative.begin();
      if (first == relative.end() || *first == ".." || *first == ".") {
        return std::nullopt;
      }
      return relative;
    };

    if (auto relative = validRelative(path.lexically_relative(directory)); relative.has_value()) {
      return relative;
    }

    // A member directory can be reached through a symlink (for example a
    // Wallpapers/Name link); resolve both sides before giving up so applying
    // from the original path still maps the linked set.
    std::error_code ec;
    const auto canonicalPath = std::filesystem::weakly_canonical(path, ec);
    if (ec) {
      return std::nullopt;
    }
    const auto canonicalDirectory = std::filesystem::weakly_canonical(directory, ec);
    if (ec) {
      return std::nullopt;
    }
    return validRelative(canonicalPath.lexically_relative(canonicalDirectory));
  }

  std::size_t pathDepth(const std::filesystem::path& path) {
    return static_cast<std::size_t>(std::distance(path.begin(), path.end()));
  }

  bool isPresentFile(const std::string& path) {
    std::error_code ec;
    return !path.empty() && std::filesystem::is_regular_file(path, ec) && !ec;
  }

} // namespace

ThemeMode wallpaper::effectiveThemeMode(ThemeMode mode, bool isLight) noexcept {
  if (mode == ThemeMode::Auto) {
    return isLight ? ThemeMode::Light : ThemeMode::Dark;
  }
  return mode;
}

const WallpaperMonitorOverride*
wallpaper::findWallpaperMonitorOverride(const WallpaperConfig& config, const WaylandOutput& output) {
  for (const auto& ovr : config.monitorOverrides) {
    if (outputMatchesSelector(ovr.match, output)) {
      return &ovr;
    }
  }
  return nullptr;
}

std::string
wallpaper::resolveWallpaperDirectory(const WallpaperConfig& config, const WaylandOutput& output, ThemeMode mode) {
  if (config.perMonitorDirectories) {
    if (const auto* ovr = findWallpaperMonitorOverride(config, output); ovr != nullptr) {
      if (mode == ThemeMode::Light && ovr->directoryLight.has_value() && !ovr->directoryLight->empty()) {
        return *ovr->directoryLight;
      }
      if (mode == ThemeMode::Dark && ovr->directoryDark.has_value() && !ovr->directoryDark->empty()) {
        return *ovr->directoryDark;
      }
      if (ovr->directory.has_value() && !ovr->directory->empty()) {
        return *ovr->directory;
      }
    }
  }
  return resolveGlobalWallpaperDirectory(config, mode);
}

std::string wallpaper::resolveGlobalWallpaperDirectory(const WallpaperConfig& config, ThemeMode mode) {
  if (mode == ThemeMode::Light && !config.directoryLight.empty()) {
    return config.directoryLight;
  }
  if (mode == ThemeMode::Dark && !config.directoryDark.empty()) {
    return config.directoryDark;
  }
  if (!config.directory.empty()) {
    return config.directory;
  }
  return FileUtils::defaultPicturesDirectory().string();
}

std::optional<wallpaper::LinkedWallpaperMapping>
wallpaper::mapLinkedWallpaper(const WallpaperConfig& config, const std::string& appliedPath) {
  if (appliedPath.empty() || appliedPath.starts_with("color:")) {
    return std::nullopt;
  }

  const std::filesystem::path file = expandedPath(appliedPath);

  // Resolve the source member by the path's member directory. The most specific
  // (deepest) directory wins when member directories are nested.
  const WallpaperLinkGroup* sourceGroup = nullptr;
  std::size_t sourceMemberIndex = 0;
  std::filesystem::path sourceRelative;
  std::size_t sourceDepth = 0;

  for (const auto& group : config.links) {
    for (std::size_t i = 0; i < group.members.size(); ++i) {
      const auto& member = group.members[i];
      if (member.monitor.empty() || member.directory.empty()) {
        continue;
      }
      const std::filesystem::path directory = expandedPath(member.directory);
      const auto relative = relativePathInside(file, directory);
      if (!relative.has_value()) {
        continue;
      }
      const std::size_t depth = pathDepth(directory);
      if (sourceGroup == nullptr || depth > sourceDepth) {
        sourceGroup = &group;
        sourceMemberIndex = i;
        sourceRelative = *relative;
        sourceDepth = depth;
      }
    }
  }

  if (sourceGroup == nullptr) {
    return std::nullopt;
  }

  LinkedWallpaperMapping mapping;
  mapping.group = sourceGroup;
  mapping.groupId = sourceGroup->id;
  mapping.groupName = sourceGroup->name.empty() ? sourceGroup->id : sourceGroup->name;
  mapping.sourceMonitor = sourceGroup->members[sourceMemberIndex].monitor;
  mapping.relativePath = sourceRelative.generic_string();

  for (const auto& member : sourceGroup->members) {
    if (member.monitor.empty()) {
      continue;
    }
    LinkedWallpaperTarget target;
    target.monitor = member.monitor;
    if (!member.directory.empty()) {
      target.path = (expandedPath(member.directory) / sourceRelative).lexically_normal().string();
      target.present = isPresentFile(target.path);
    }
    if (target.present) {
      mapping.appliedConnectors.push_back(target.monitor);
    } else {
      mapping.missingConnectors.push_back(target.monitor);
    }
    mapping.targets.push_back(std::move(target));
  }

  const auto presentPathForMonitor = [&mapping](const std::string& monitor) -> const std::string* {
    for (const auto& target : mapping.targets) {
      if (target.present && target.monitor == monitor) {
        return &target.path;
      }
    }
    return nullptr;
  };

  // Palette generation follows the selected color-source member. An empty
  // color_source means the first member; if its file is missing, fall back to
  // the clicked source member, then the first present member.
  std::string wantedSource;
  if (!sourceGroup->colorSource.empty()) {
    wantedSource = sourceGroup->colorSource;
  } else {
    for (const auto& member : sourceGroup->members) {
      if (!member.monitor.empty() && !member.directory.empty()) {
        wantedSource = member.monitor;
        break;
      }
    }
  }

  if (const std::string* path = presentPathForMonitor(wantedSource); path != nullptr) {
    mapping.colorSourceMonitor = wantedSource;
    mapping.colorSourcePath = *path;
  } else if (const std::string* path = presentPathForMonitor(mapping.sourceMonitor); path != nullptr) {
    mapping.colorSourceMonitor = mapping.sourceMonitor;
    mapping.colorSourcePath = *path;
  } else {
    for (const auto& target : mapping.targets) {
      if (target.present) {
        mapping.colorSourceMonitor = target.monitor;
        mapping.colorSourcePath = target.path;
        break;
      }
    }
  }

  return mapping;
}
