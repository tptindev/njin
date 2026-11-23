# Njin development

- Before implementing engine features, check existing Njin code and dependencies for
  reusable components. For renderer work, consult the upstream candidates in
  [Renderer reuse decisions](docs/renderer-reuse.md) before designing a new solution.
- Prefer compatible algorithms, tooling and regression tests that fit the existing
  Njin API, SDL3 platform layer and bgfx backend. Compare the maintenance and
  verification cost of adapting code with implementing the feature locally.
- Before importing or translating third-party code, pin the revision, verify
  file-specific licenses, and record provenance, modifications and notices in
  [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
- Exercise adapted features through Njin's normal entry points. Record which
  backends compiled and which passed runtime checks.
