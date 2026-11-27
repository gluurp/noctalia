#include "config/config_types.h"
#include "config/schema/config_schema.h"
#include "config/schema/config_schema_fields.h"
#include "config/schema/diagnostics.h"
#include "config/schema/engine.h"
#include "config/schema/ranges.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>

namespace noctalia::config::schema {

  const Schema<BackdropConfig>& backdropSchema() {
    static const Schema<BackdropConfig> s = {
        field(&BackdropConfig::enabled, "enabled"),
        field(&BackdropConfig::blurIntensity, "blur_intensity", kUnitRange),
        field(&BackdropConfig::tintIntensity, "tint_intensity", kUnitRange),
    };
    return s;
  }

  const Schema<LockscreenConfig>& lockscreenSchema() {
    static const Schema<LockscreenConfig> s = {
        field(&LockscreenConfig::enabled, "enabled"),
        field(&LockscreenConfig::lockBeforeSuspend, "lock_before_suspend"),
        field(&LockscreenConfig::dnd, "dnd"),
        field(&LockscreenConfig::gracePeriodSeconds, "grace_period_seconds", Range<std::int64_t>{0, 60}),
        field(&LockscreenConfig::fingerprint, "fingerprint"),
        field(&LockscreenConfig::allowEmptyPassword, "allow_empty_password"),
        field(&LockscreenConfig::blurredDesktop, "blurred_desktop"),
        enumArrayField(
            &LockscreenConfig::transitions, "transition", kLockscreenTransitions, std::optional<LockscreenTransition>{}
        ),
        field(&LockscreenConfig::transitionDurationMs, "transition_duration", kLockscreenTransitionDurationRange),
        field(&LockscreenConfig::edgeSmoothness, "edge_smoothness", kUnitRange),
        field(&LockscreenConfig::blurIntensity, "blur_intensity", kUnitRange),
        field(&LockscreenConfig::tintIntensity, "tint_intensity", kUnitRange),
        colorField(&LockscreenConfig::tintColor, "tint_color"),
        pathStringField(&LockscreenConfig::wallpaper, "wallpaper"),
        field(&LockscreenConfig::monitors, "monitors"),
    };
    return s;
  }

  namespace {
    const Schema<DesktopWidgetsGridState>& desktopWidgetsGridSchema() {
      static const Schema<DesktopWidgetsGridState> s = {
          field(&DesktopWidgetsGridState::visible, "visible"),
          field(&DesktopWidgetsGridState::cellSize, "cell_size", Range<std::int64_t>{8, 256}),
          field(&DesktopWidgetsGridState::majorInterval, "major_interval", Range<std::int64_t>{1, 16}),
      };
      return s;
    }
  } // namespace

  const Schema<DesktopWidgetsConfig>& desktopWidgetsSchema() {
    static const Schema<DesktopWidgetsConfig> s = {
        field(&DesktopWidgetsConfig::enabled, "enabled"),
        field(&DesktopWidgetsConfig::schemaVersion, "schema_version"),
        subTable(&DesktopWidgetsConfig::grid, "grid", desktopWidgetsGridSchema()),
    };
    return s;
  }

  const Schema<LockscreenWidgetsConfig>& lockscreenWidgetsSchema() {
    static const Schema<LockscreenWidgetsConfig> s = {
        field(&LockscreenWidgetsConfig::enabled, "enabled"),
        field(&LockscreenWidgetsConfig::schemaVersion, "schema_version"),
        subTable(&LockscreenWidgetsConfig::grid, "grid", desktopWidgetsGridSchema()),
    };
    return s;
  }

  const Schema<HotCornersConfig>& hotCornersSchema() {
    static const Schema<HotCornersConfig::Corner> cornerSchema = {
        field(&HotCornersConfig::Corner::action, "action"),
        field(&HotCornersConfig::Corner::command, "command"),
    };
    static const Schema<HotCornersConfig> s = {
        field(&HotCornersConfig::enabled, "enabled"),
        field(&HotCornersConfig::delayMs, "delay_ms", kHotCornersDelayMsRange),
        subTable(&HotCornersConfig::topLeft, "top_left", cornerSchema),
        subTable(&HotCornersConfig::topRight, "top_right", cornerSchema),
        subTable(&HotCornersConfig::bottomLeft, "bottom_left", cornerSchema),
        subTable(&HotCornersConfig::bottomRight, "bottom_right", cornerSchema),
    };
    return s;
  }

  namespace {
    const Schema<WallpaperAutomationConfig>& wallpaperAutomationSchema() {
      static const Schema<WallpaperAutomationConfig> s = {
          field(&WallpaperAutomationConfig::enabled, "enabled"),
          field(&WallpaperAutomationConfig::intervalSeconds, "interval_seconds", kWallpaperAutomationIntervalRange),
          // order accepts case-insensitive random|alphabetical.
          custom<WallpaperAutomationConfig>(
              "order",
              [](const toml::table& tbl, WallpaperAutomationConfig& out, std::string_view parentPath,
                 Diagnostics& diag) {
                if (auto v = tbl["order"].value<std::string>()) {
                  const std::string lowered = StringUtils::toLower(StringUtils::trim(*v));
                  if (auto parsed = enumFromKey(kWallpaperAutomationOrders, lowered)) {
                    out.order = *parsed;
                  } else {
                    diag.warn(joinPath(parentPath, "order"), "expected random|alphabetical, got \"" + *v + "\"");
                  }
                } else {
                  warnWrongType(
                      tbl, "order", parentPath, diag,
                      enumKeyList(kWallpaperAutomationOrders, std::size(kWallpaperAutomationOrders))
                  );
                }
              },
              [](toml::table& tbl, const WallpaperAutomationConfig& in) {
                tbl.insert_or_assign("order", std::string(enumToKey(kWallpaperAutomationOrders, in.order)));
              }
          ),
          field(&WallpaperAutomationConfig::recursive, "recursive"),
          field(&WallpaperAutomationConfig::pauseOnFullscreen, "pause_on_fullscreen"),
      };
      return s;
    }

    const Schema<WallpaperMonitorOverride>& wallpaperMonitorSchema() {
      static const Schema<WallpaperMonitorOverride> s = {
          field(&WallpaperMonitorOverride::match, "match"),
          optionalBoolField(&WallpaperMonitorOverride::enabled, "enabled"),
          colorSpecField(&WallpaperMonitorOverride::fillColor, "fill_color", /*alwaysEmit=*/false),
          optionalPathStringField(&WallpaperMonitorOverride::directory, "directory"),
          optionalPathStringField(&WallpaperMonitorOverride::directoryLight, "directory_light"),
          optionalPathStringField(&WallpaperMonitorOverride::directoryDark, "directory_dark"),
      };
      return s;
    }

    // An optional array-of-tables field: absent means "no entries", and an empty
    // vector writes nothing instead of `member = []`.
    template <typename Parent, typename Elem>
    Field<Parent> optionalArrayOf(
        std::vector<Elem> Parent::* member, std::string_view key, const Schema<Elem>& subSchema,
        std::function<bool(const Elem&)> keep
    ) {
      Field<Parent> f = arrayOf<Parent, Elem>(member, key, subSchema, keep);
      auto baseRead = f.read;
      auto baseWrite = f.write;
      f.read = [baseRead = std::move(baseRead), key](
                   const toml::table& tbl, Parent& out, std::string_view parentPath, Diagnostics& diag
               ) {
        if (!tbl.contains(key)) {
          return;
        }
        baseRead(tbl, out, parentPath, diag);
      };
      f.write = [baseWrite = std::move(baseWrite), key, member](toml::table& tbl, const Parent& in) {
        if (!tbl.contains(key) && (in.*member).empty()) {
          return;
        }
        if ((in.*member).empty()) {
          tbl.erase(key);
          return;
        }
        baseWrite(tbl, in);
      };
      return f;
    }

    template <typename Struct>
    Field<Struct> optionalEnumStringField(
        std::optional<ThemeMode> Struct::* member, std::string_view key, const EnumOption<ThemeMode>* options,
        std::size_t optionCount
    ) {
      return custom<Struct>(
          key,
          [member, key, options, optionCount](
              const toml::table& tbl, Struct& out, std::string_view parentPath, Diagnostics& diag
          ) {
            if (auto v = tbl[key].value<std::string>()) {
              const std::string lowered = StringUtils::toLower(StringUtils::trim(*v));
              std::optional<ThemeMode> parsed;
              for (std::size_t i = 0; i < optionCount; ++i) {
                if (options[i].key == lowered) {
                  parsed = options[i].value;
                  break;
                }
              }
              if (parsed.has_value()) {
                out.*member = *parsed;
              } else {
                diag.warn(joinPath(parentPath, key), "unknown value \"" + *v + "\"");
              }
            } else if (tbl.contains(key)) {
              warnWrongType(tbl, key, parentPath, diag, "a string");
            }
          },
          [member, key, options, optionCount](toml::table& tbl, const Struct& in) {
            if (!(in.*member).has_value()) {
              return;
            }
            for (std::size_t i = 0; i < optionCount; ++i) {
              if (options[i].value == *(in.*member)) {
                tbl.insert_or_assign(key, std::string(options[i].key));
                return;
              }
            }
          }
      );
    }

    template <typename Struct>
    Field<Struct> optionalEnumStringField(
        std::optional<PaletteSource> Struct::* member, std::string_view key,
        const EnumOption<PaletteSource>* options, std::size_t optionCount
    ) {
      return custom<Struct>(
          key,
          [member, key, options, optionCount](
              const toml::table& tbl, Struct& out, std::string_view parentPath, Diagnostics& diag
          ) {
            if (auto v = tbl[key].value<std::string>()) {
              const std::string lowered = StringUtils::toLower(StringUtils::trim(*v));
              std::optional<PaletteSource> parsed;
              for (std::size_t i = 0; i < optionCount; ++i) {
                if (options[i].key == lowered) {
                  parsed = options[i].value;
                  break;
                }
              }
              if (parsed.has_value()) {
                out.*member = *parsed;
              } else {
                diag.warn(joinPath(parentPath, key), "unknown value \"" + *v + "\"");
              }
            } else if (tbl.contains(key)) {
              warnWrongType(tbl, key, parentPath, diag, "a string");
            }
          },
          [member, key, options, optionCount](toml::table& tbl, const Struct& in) {
            if (!(in.*member).has_value()) {
              return;
            }
            for (std::size_t i = 0; i < optionCount; ++i) {
              if (options[i].value == *(in.*member)) {
                tbl.insert_or_assign(key, std::string(options[i].key));
                return;
              }
            }
          }
      );
    }

    const Schema<WallpaperLinkMember>& wallpaperLinkMemberSchema() {
      static const Schema<WallpaperLinkMember> s = {
          pathStringField(&WallpaperLinkMember::monitor, "monitor"),
          pathStringField(&WallpaperLinkMember::directory, "directory"),
          optionalPathStringField(&WallpaperLinkMember::directoryLight, "directory_light"),
          optionalPathStringField(&WallpaperLinkMember::directoryDark, "directory_dark"),
      };
      return s;
    }

    const Schema<WallpaperLinkGroup>& wallpaperLinkGroupSchema() {
      static const Schema<WallpaperLinkGroup> s = {
          field(&WallpaperLinkGroup::name, "name"),
          field(&WallpaperLinkGroup::colorSource, "color_source"),
          field(&WallpaperLinkGroup::hideOriginals, "hide_originals"),
          optionalArrayOf<WallpaperLinkGroup, WallpaperLinkMember>(
              &WallpaperLinkGroup::members, "member", wallpaperLinkMemberSchema(),
              [](const WallpaperLinkMember& m) { return !m.monitor.empty() && !m.directory.empty(); }
          ),
          finalize<WallpaperLinkGroup>([](WallpaperLinkGroup& group, std::string_view parentPath, Diagnostics& diag) {
            if (!group.colorSource.empty()
                && std::ranges::none_of(
                    group.members, [&](const WallpaperLinkMember& m) { return m.monitor == group.colorSource; }
                )) {
              diag.warn(
                  joinPath(parentPath, "color_source"),
                  "does not name a member monitor (\"" + group.colorSource + "\")"
              );
            }
            std::unordered_set<std::string> seen;
            for (const auto& member : group.members) {
              if (!seen.insert(member.monitor).second) {
                diag.warn(
                    joinPath(parentPath, "member"), "monitor \"" + member.monitor + "\" appears more than once"
                );
              }
            }
          }),
      };
      return s;
    }

    const Schema<WallpaperColorOverride>& wallpaperColorOverrideSchema() {
      static const Schema<WallpaperColorOverride> s = {
          pathStringField(&WallpaperColorOverride::path, "path"),
          field(&WallpaperColorOverride::linkGroup, "link_group"),
          field(&WallpaperColorOverride::match, "match"),
          optionalEnumStringField(
              &WallpaperColorOverride::themeMode, "theme_mode", kThemeModes, std::size(kThemeModes)
          ),
          optionalEnumStringField(
              &WallpaperColorOverride::paletteSource, "palette_source", kPaletteSources, std::size(kPaletteSources)
          ),
          field(&WallpaperColorOverride::builtinPalette, "builtin_palette"),
          field(&WallpaperColorOverride::communityPalette, "community_palette"),
          field(&WallpaperColorOverride::customPalette, "custom_palette"),
          field(&WallpaperColorOverride::wallpaperScheme, "wallpaper_scheme"),
          finalize<WallpaperColorOverride>(
              [](WallpaperColorOverride& overrideValue, std::string_view parentPath, Diagnostics& diag) {
                const bool hasPath = !overrideValue.path.empty();
                const bool hasSet = !overrideValue.linkGroup.empty() || !overrideValue.match.empty();
                if (hasPath && hasSet) {
                  diag.warn(std::string(parentPath), "path and link_group/match are mutually exclusive");
                }
                if (!hasPath && (!overrideValue.linkGroup.empty() != !overrideValue.match.empty())) {
                  diag.warn(std::string(parentPath), "link_group and match must be provided together");
                }
              }
          ),
      };
      return s;
    }

  } // namespace

  const Schema<WallpaperConfig>& wallpaperSchema() {
    static const Schema<WallpaperConfig> s = {
        field(&WallpaperConfig::enabled, "enabled"),
        enumField(&WallpaperConfig::fillMode, "fill_mode", kWallpaperFillModes),
        colorSpecField(&WallpaperConfig::fillColor, "fill_color", /*alwaysEmit=*/true),
        enumArrayField(
            &WallpaperConfig::transitions, "transition", kWallpaperTransitions, std::optional<WallpaperTransition>{}
        ),
        field(&WallpaperConfig::transitionDurationMs, "transition_duration", kWallpaperTransitionDurationRange),
        field(&WallpaperConfig::edgeSmoothness, "edge_smoothness", kUnitRange),
        field(&WallpaperConfig::transitionOnStartup, "transition_on_startup"),
        field(&WallpaperConfig::syncTransitions, "sync_transitions"),
        pathStringField(&WallpaperConfig::directory, "directory"),
        pathStringField(&WallpaperConfig::directoryLight, "directory_light"),
        pathStringField(&WallpaperConfig::directoryDark, "directory_dark"),
        field(&WallpaperConfig::perMonitorDirectories, "per_monitor_directories"),
        field(&WallpaperConfig::showFavoritesAtRoot, "show_favorites_at_root"),
        subTable(&WallpaperConfig::automation, "automation", wallpaperAutomationSchema()),
        namedMap<WallpaperConfig, WallpaperMonitorOverride>(
            &WallpaperConfig::monitorOverrides, "monitor", wallpaperMonitorSchema(),
            [](WallpaperMonitorOverride& o, std::string_view name) { o.match = std::string(name); },
            [](const WallpaperMonitorOverride& o) { return o.match; }
        ),
        namedMap<WallpaperConfig, WallpaperLinkGroup>(
            &WallpaperConfig::links, "link", wallpaperLinkGroupSchema(),
            [](WallpaperLinkGroup& group, std::string_view name) { group.id = std::string(name); },
            [](const WallpaperLinkGroup& group) { return group.id; }
        ),
        optionalArrayOf<WallpaperConfig, WallpaperColorOverride>(
            &WallpaperConfig::entries, "entry", wallpaperColorOverrideSchema(),
            [](const WallpaperColorOverride& entry) { return !entry.path.empty(); }
        ),
        optionalArrayOf<WallpaperConfig, WallpaperColorOverride>(
            &WallpaperConfig::colorOverrides, "color_override", wallpaperColorOverrideSchema(),
            [](const WallpaperColorOverride& overrideValue) {
              return !overrideValue.path.empty()
                  || (!overrideValue.linkGroup.empty() && !overrideValue.match.empty());
            }
        ),
    };
    return s;
  }

} // namespace noctalia::config::schema
