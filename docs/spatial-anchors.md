# Persistent spatial anchors

App 08 demonstrates one locally persisted, world-locked marker on Meta Quest.
It uses `XR_FB_spatial_entity` for anchor creation and components,
`XR_META_spatial_entity_persistence` for save/erase, and the UUID-filtered
`XR_FB_spatial_entity_query` API for restore.

## Anchor lifecycle

An `XrSpace` is a live runtime handle. Destroying it releases that handle but
does not erase a persisted anchor. Saving creates a persistent runtime record;
erasing removes that record. The app stores the corresponding UUID in its
private data directory only after save completes successfully.

On relaunch, the app reads that UUID and issues a query for exactly that
anchor. It never substitutes a `LOCAL` pose if restore fails. Uninstalling the
app removes its UUID association; the runtime anchor can consequently become
orphaned because this single-anchor sample intentionally does not run broad
discovery.

## Permission and API choice

The manifest declares `com.oculus.permission.USE_ANCHOR_API`. Quest may show a
spatial-data consent prompt. Local persistence does not require
`IMPORT_EXPORT_IOT_MAP_DATA`.

The repository pins OpenXR Android 1.1.51, whose headers do not include
`XR_META_spatial_entity_discovery`. The portable `XR_EXT_spatial_anchor`
backend is also deferred because it was not advertised by the Quest runtime
used for the preceding milestone.

## Controls and diagnostics

- Trigger creates an anchor at the controller preview.
- A or X erases the saved anchor.
- Yellow means ready to place; amber means pending; bright green means tracked;
  dim green means valid but inferred; red means an operation failed.
- An invalid anchor pose is hidden until localization recovers.

Build and run:

```bash
./scripts/build_deploy.sh --app 08-spatial-anchors
adb logcat -s SpatialAnchors:V OpenXR:V '*:S'
```

Common failures include denied permission, insufficiently mapped surroundings,
poor lighting, storage failure, tracking loss, and platform rate limits. Keep
content close to its anchor; Meta recommends a distance of no more than about
three metres.

Validation must include force-stop/relaunch, headset reboot, temporary tracking
loss, erase/relaunch, and repeated create/save/restore/erase cycles.
