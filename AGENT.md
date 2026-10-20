# AGENT.md

> This file instructs any AI coding agent (OpenCode, Claude Code, Codex, etc.)
> working in this repository to always reference and comply with the project
> documentation in `docs/` before and during all development work.

---

## Mandatory Reading Before Writing Code

**Before making any code changes**, read the following documents in order:

1. **`docs/PRD.md`** — Understand what is in scope for the current phase.
   Do not implement P2 features during P0/P1 work. Check acceptance criteria
   before claiming a feature is "done".

2. **`docs/ARCHITECTURE.md`** — Understand system data flow, task architecture,
   core split, memory layout, and error recovery. Any new task, buffer, or
   component must fit this architecture. If a change requires an architecture
   deviation, document it in this file first (update the doc, then write code).

3. **`docs/TECH_STACK.md`** — Use the exact component versions, GPIO pin
   numbers, and API calls listed here. Do not substitute a different GPIO,
   sample rate, or component version without updating this file. The GPIO
   mapping table in Section 5 is authoritative for all hardware pins.

4. **`docs/CONVENTIONS.md`** — Follow all naming, CMakeLists, logging,
   memory, FreeRTOS task, sdkconfig, NVS, and git conventions. Do not deviate
   from the directory structure in Section 1. Do not use C++ — this is a
   C11 project. Use the exact TAG strings in Section 5.1 for logging.

---

## Workflow Rules

### Phase Gates

- Do not start Phase N+1 work until all P0 acceptance criteria for Phase N are
  verified (hardware smoke test + serial log + visual check).
- If a blocker is found, stop and report it; do not "work around" it by
  skipping a phase.
- Phase 1 exit = M1 milestone: display boots, RTL-SDR detected, IQ streaming
  confirmed in serial log.

### File Ownership

- `firmware/CMakeLists.txt` — top-level boilerplate only. Do not add logic.
- `firmware/main/CMakeLists.txt` — update `SRCS` list when adding/removing
  source files. Follow the pattern in CONVENTIONS.md Section 3.
- `firmware/sdkconfig.defaults` — base config for ESP32-P4. Do not commit
  `firmware/sdkconfig` (generated file).
- `firmware/main/idf_component.yml` — pin component versions as listed in
  TECH_STACK.md Section 3.1. Do not add new dependencies without checking
  ARCHITECTURE.md Section 10 (dependency graph).

### Memory Rules

- All large buffers (> 64 KB) must use `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`.
- Hot DSP scratch (FFT twiddle, window tables) must use `MALLOC_CAP_INTERNAL`.
- Never use bare `malloc()` / `free()` in firmware code.

### Task Rules

- Pin new tasks to the correct core per ARCHITECTURE.md Section 2 table.
- Do not increase task priorities above the values in the table without
  re-evaluating preemption.
- Any new task must have a TAG entry in CONVENTIONS.md Section 5.1.

### Logging

- Use `ESP_LOGE/W/I/D` only — no `printf`, no `ESP_LOGV` in production.
- TAG strings must match CONVENTIONS.md Section 5.1 exactly.
- No per-sample logging. Periodic metrics: minimum 1 Hz via `esp_timer`.

### Error Recovery

- Follow the recovery table in ARCHITECTURE.md Section 9.
- USB dongle unplug: `stop()` → 1 s delay → `start_hz()`. Never `abort()`.
- FAULT state: `reset()` → `start_hz()`.
- PSRAM allocation failure: `ESP_LOGE` + `abort()` with coredump enabled.

### Git

- Commit messages: `<type>: <description>` (see CONVENTIONS.md Section 10).
- Never commit `firmware/sdkconfig`, `firmware/build/`,
  `firmware/managed_components/`.
- Commit docs/ changes in the same PR as the code they document.

---

## Reference Documents Quick Index

| Question to answer | Look in |
|-------------------|---------|
| What should I build this phase? | `docs/PRD.md` — Phase section |
| How does the data flow work? | `docs/ARCHITECTURE.md` — Section 1, 2, 4 |
| Which GPIO is used for what? | `docs/TECH_STACK.md` — Section 5 |
| What version of which component? | `docs/TECH_STACK.md` — Section 3 |
| How do I name a new function? | `docs/CONVENTIONS.md` — Section 2 |
| What CMakeLists pattern to use? | `docs/CONVENTIONS.md` — Section 3 |
| What TAG string for logging? | `docs/CONVENTIONS.md` — Section 5.1 |
| What NVS keys are defined? | `docs/CONVENTIONS.md` — Section 9 |
| What sample rates are valid? | `docs/TECH_STACK.md` — Section 6 |
| How to handle USB unplug? | `docs/ARCHITECTURE.md` — Section 9 |
| Where does each source file go? | `docs/CONVENTIONS.md` — Section 1 |

---

## If Documentation and Code Disagree

**The documentation is the source of truth.** If you find code that deviates
from `docs/`:

1. Stop and report the discrepancy.
2. Do not silently "fix" the code to match the doc — the doc may be stale.
3. Do not silently "fix" the doc to match the code — the code may be wrong.
4. Ask the user which is correct, then update the doc first, then fix the code.

---

## Current Development Phase

> *Update this line as phases complete.*

**Current phase:** Phase 0 (planning complete, no code written yet)

| Phase | Status |
|-------|--------|
| Phase 0: Planning & docs | ✅ Complete |
| Phase 1: BSP + SDR bring-up | ⬜ Not started |
| Phase 2: DSP pipeline | ⬜ Not started |
| Phase 3: UI dashboards | ⬜ Not started |
| Phase 4: FM radio + RDS | ⬜ Not started |
| Phase 5: Polish + features | ⬜ Not started |
