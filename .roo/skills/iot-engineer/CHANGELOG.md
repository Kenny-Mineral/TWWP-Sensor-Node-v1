---
name: iot-engineer-changelog
description: Change history for the IoT Engineer mode and related global Roo configurations.
---

# IoT Engineer Mode — Changelog

## 2026-04-27: Made global

Both TWWP custom Roo modes were promoted from project-local (Waveshare build TWWP) to global scope:

### 📡 IoT Engineer
- **Mode definition:** Added to `~/.config/Code/User/globalStorage/rooveterinaryinc.roo-cline/settings/custom_modes.yaml`
- **Rules:** Copied to `~/.roo/rules-iot-engineer/` (3 XML files: workflow, best_practices, decision_guidance)
- **Skills:** Copied to `~/.roo/skills/iot-engineer/` (8 MD files: SKILL.md + 7 reference docs)
- **Project-local cleanup:** Removed `.roomodes` and `.roo/` directory from TWWP Sensor Node v1 workspace

### 📚 IoT PDF Researcher
- **Mode definition:** Added to global `custom_modes.yaml`
- **Rules:** Copied to `~/.roo/rules-iot-pdf-researcher/` (3 XML files)
- **No skills directory** — this mode uses MCP tools directly (pdf_info, pdf_search, pdf_read_pages, pdf_render_pages)

### Precedence note
Project-local `.roomodes` overrides global modes for the same slug. The Waveshare project still has its own `.roomodes` with both modes, so those take precedence when working in that workspace.