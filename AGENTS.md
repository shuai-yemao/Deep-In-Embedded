# Repository Guidelines

## Project Structure & Module Organization

This repository is an Obsidian knowledge base for embedded systems. Content is organized by domain:

- `开发板/`: board- and MCU-specific notes, such as STM32 and ESP32-S3 topics.
- `常用模块/`: reusable peripheral and module notes, including sensors, displays, storage, and drivers.
- `通信协议/`: wired, wireless, internet, and IoT protocol references.
- `操作系统/`: RTOS and operating-system notes.
- `中间件/`: middleware notes such as LVGL, CmBacktrace, and SEGGER SystemView.
- `嵌入式项目文档/`: project workflows, troubleshooting templates, reports, and issue records.

Place images and other note-local assets in an `assets/` folder beside the related note or topic directory. Keep large PDFs only when they are directly referenced by notes.

## Build, Test, and Development Commands

This is a Markdown documentation repository, so there is no application build step.

- `git status --short --branch`: inspect local changes before syncing.
- `git pull --rebase`: update from GitHub before editing when working outside Obsidian.
- `git add . && git commit -m "embedded notes: YYYY-MM-DD HH:mm:ss"`: manually commit note updates if Obsidian Git is unavailable.
- `git push`: publish committed notes to the remote repository.

## Writing Style & Naming Conventions

Use Markdown files with the `.md` extension. Prefer descriptive topic names, for example `SPI.md`, `FreeRTOS的命名规则.md`, or `2026-05-03-串口发送首字节丢失.md`. For issue records, use date-prefixed names under `嵌入式项目文档/问题记录/<project>/`. Keep headings hierarchical, use fenced code blocks for commands or C snippets, and use relative Obsidian links for local references.

## Testing Guidelines

There are no automated tests. Before committing, open changed notes in Obsidian and verify that links, images, Mermaid diagrams, tables, and embedded PDFs render correctly. For troubleshooting reports or HTML exports, confirm the file opens locally and that referenced assets are present.

## Commit & Pull Request Guidelines

The current history uses automated messages like `embedded notes: 2026-06-30 14:38:07`. Use the same format for routine sync commits. For larger reorganizations, use a clearer message such as `reorganize STM32F411 notes` and describe moved paths in the PR. Pull requests should summarize changed topics, mention deleted or renamed notes, and include screenshots only when visual diagrams or rendered reports changed.

## Security & Configuration Tips

Do not commit credentials, API keys, private device logs, or proprietary firmware binaries. Keep machine-specific Obsidian settings out of this repository unless they are required for shared rendering behavior. Review large files before pushing because GitHub has file size limits.
