# web_ui

A new Flutter project.

## Getting Started

This project is a starting point for a Flutter application.

A few resources to get you started if this is your first Flutter project:

- [Lab: Write your first Flutter app](https://docs.flutter.dev/get-started/codelab)
- [Cookbook: Useful Flutter samples](https://docs.flutter.dev/cookbook)

For help getting started with Flutter development, view the
[online documentation](https://docs.flutter.dev/), which offers tutorials,
samples, guidance on mobile development, and a full API reference.

## UI Notes

### Chart Tooltips
Native SVG charts (line + bar) render hover tooltips that follow the cursor and show formatted x/y
values. Tooltip formatting uses any provided label builders on the chart; otherwise values are
rendered with default numeric formatting. Tooltips hide on mouse leave and ignore non-finite values.

### Verbose / Diagnostic Mode
Verbose mode is opt-in. Toggle it in the top app bar or via `?verbose=1` in the URL. When enabled,
trace-backed diagnostics render as a collapsible stepper inside debug panels, showing intermediate
artifacts (inputs/outputs/prompt/response) with size badges and truncation controls. This mode does
not change agent responses and is safe-by-default unless explicitly enabled.
