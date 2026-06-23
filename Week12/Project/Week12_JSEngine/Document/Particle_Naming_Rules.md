# Particle Naming Rules

## Utils

Use `Utils` for stateless runtime calculations, sampling, conversion, and small pure functions.

Examples:

- `ParticleModuleUtils`
- `ParticleRibbonUtils`
- `ParticleUpdateUtils.h`

## Helpers

Use `Helpers` only for editor-side workflow classes that coordinate several systems or wrap UI procedures.

Examples:

- `FEditorMainPanelPlacementHelpers`
- `FEditorMainPanelPackagingHelpers`
- `FEditorMainPanelViewportToolbarHelpers`

## Util

Do not introduce new singular `Util` names. Existing shared names such as `MathUtil` are legacy/common engine API and can stay.

## Anonymous Namespace

Avoid anonymous namespaces for new particle helper code. Prefer one of these:

- `private static` member functions when the logic belongs to one class
- named `Particle*Utils` namespaces for stateless file-local runtime helpers
- small named structs when the helper has meaningful state
