/*
 * Hydra firmware version — single source of truth.
 *
 * Bumped by the `/bump-hydra` Claude skill (see .claude/commands/bump-hydra.md)
 * before each commit. Do NOT hand-edit the numeric macros unless you also
 * update CHANGELOG.md to match.
 *
 * Semver convention for this project:
 *   PATCH (0.0.x) — bug fixes, doc fixes, refactors with no user-visible
 *                   feature change, defensive cleanup.
 *   MINOR (0.x.0) — new feature, new menu entry, new SD-file format,
 *                   added Marauder tool, anything a user would notice.
 *   MAJOR (x.0.0) — breaking changes to on-device behaviour, SD layout,
 *                   pin map, or "I'd have to re-learn the firmware" UX.
 *
 * v1.0.0 is the marker for "feature set considered complete and stable
 * on DIV v1 hardware" — bumping major from 0 to 1 is a one-time event.
 */

#ifndef HYDRA_VERSION_H
#define HYDRA_VERSION_H

#define HYDRA_VERSION_MAJOR 0
#define HYDRA_VERSION_MINOR 2
#define HYDRA_VERSION_PATCH 0

// Stringification dance: turn numeric macros into a "0.0.1" literal.
#define HYDRA_STRINGIFY_INNER(x) #x
#define HYDRA_STRINGIFY(x) HYDRA_STRINGIFY_INNER(x)
#define HYDRA_VERSION HYDRA_STRINGIFY(HYDRA_VERSION_MAJOR) "." \
                      HYDRA_STRINGIFY(HYDRA_VERSION_MINOR) "." \
                      HYDRA_STRINGIFY(HYDRA_VERSION_PATCH)

#endif // HYDRA_VERSION_H
