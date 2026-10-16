# Yxml source provenance

Unmodified Yxml C sources and MIT license by Yoran Heling.

- Upstream: https://g.blicky.net/yxml.git
- Documentation: https://dev.yorhel.nl/yxml/man
- Pinned commit: `66507906673bc6159d5d620414479954c9c21c24`
- Retrieved from the Fuchsia upstream mirror: https://fuchsia.googlesource.com/third_party/yxml/+/66507906673bc6159d5d620414479954c9c21c24/
- The upstream HEAD query returned the same commit on 2026-10-11.
- SHA-256 yxml.c: `7699a9c0e9d9b987b74ff86bf3fc28be6deed062d22d9a0f8376b868b15864e5`
- SHA-256 yxml.h: `6f79158a3811bf1fab575ef512b91ffc87a795f070b562f7fb6bcbf71fdd9078`
- SHA-256 COPYING: `b9b5ca63904b3cf589d5719be2968436550914dc3fdf5d81b37ddbdd67986d06`

Keep vendor files unchanged. Application wrappers must enforce UTF-8/Unicode,
namespace and duplicate-attribute policy, reject DTD/entity declarations, and
apply document/depth/element budgets. Yxml itself does not provide all these
checks. No firmware or global workspace is instantiated by this addition.

`library.json` packages this pinned source for local PlatformIO use. Its
`0.0.0` version is a local packaging identifier, not an upstream release.
