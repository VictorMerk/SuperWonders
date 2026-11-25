# Frame generation handoff
The implementation and follow-up measurements are on [perf/framegen-followup](https://github.com/VictorMerk/SuperWonders/tree/perf/framegen-followup). The later [gear-menu regression](https://github.com/VictorMerk/SuperWonders/commit/350635d) documents a known limit.
Start from an opt-in presentation experiment. Integrate with the current swapchain and HDR path, then test output cadence, menus, pause/resume and frame pacing at the same graphics settings with generation disabled and enabled.
