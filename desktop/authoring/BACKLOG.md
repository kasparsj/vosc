# VOSC Editor backlog

The original milestones and their acceptance evidence are in the [M1–M5 archive](archive/roadmap-m1-m5.md).

## Feature follow-ups

- **Two-output Hydra** is in progress. Qualify pass order, texture routing, feedback history, and standalone replay for the two-output slice. [Work in progress evidence](../../.omx/evidence/vosc-editor-two-output/)
- **Audio or pointer callback sources.** Define sampling, source loss, and frozen export for the first source selected.
- **Media and Triode-specific 3D.** Define the resource model before adding support.
- **Timeline.** Plan separate tracks with independent layers and overlapping clips. Verify scheduling, transitions, and hold and resume after Editor or network loss.
- **AI-assisted authoring and broader Linux support.** Define scope and qualification for each before implementation.

## Deferred qualification and release decisions

- **VoiceOver.** Verify announcements and focus through the signed Editor, dialogs, and Problems journey. [M1 acceptance amendment](../../.omx/evidence/vosc-editor-roadmap-m1-m2/m1-candidate-05/acceptance-note.md)
- **Live Shadertoy API.** Exercise the signed import path with the live provider. [M1 fixture coverage](../../.omx/evidence/vosc-editor-roadmap-m1-m2/m1-candidate-05/m1-acceptance.md)
- **Networking.** Verify callback delivery and disconnect on direct LAN before claiming that route. The callback run used Tailnet. The separate two-Mac LAN scenario from the original baseline remains deferred. The M4 WAN-disabled check was explicitly waived. [Callback qualification](../../.omx/evidence/vosc-editor-callbacks/native-qa-evidence.md) · [M4 waiver](../../.omx/evidence/vosc-editor-triode-single-pass/accepted-wan-waiver.md)
- **Release packaging and Triode redistribution.** Record the distribution decision for the pinned compiler and define release packaging. [Packaging disposition](../../.omx/evidence/vosc-editor-triode-single-pass/TS-01-packaging/README.md)
