# ESP Display Present

Renderer-independent presentation and tearing-control helpers for ESP LCD panels.
The public Presenter owns frame and buffer lifetimes; renderer-specific LVGL
integration is provided by the sibling `esp_lv_present` component.

RGB/MIPI use multi-framebuffer scanout with explicit retirement. Single-FB/NONE
scanout is unsupported; AUTO selects a supported double/triple PARTIAL mode from
the driver pool. Panel IO supports GRAM NONE and TE synchronization. GRAM NONE
permits tearing; TE depends on the panel timing and available transfer window.

Applications include the public headers in `include/`. Internal `src/` headers
are not a compatibility contract. See [migration and SDK requirements](COMPATIBILITY.md),
[mode and buffer contracts](docs/MODE_ARCHITECTURE.md),
[state and ownership rules](STATE_MACHINE.md), and [TE timing](docs/TE_TIMING.md).
The 2.0.0 version declaration does not imply Registry publication.

RGB protocol selection is internal: confirmed bounce-buffer completion and
supported non-bounce link-switch facts have different retirement boundaries.
A generic borrowed panel handle does not establish non-bounce support. The
adapter does not install SDK patches or globally intercept LCD calls.

`CONFIG_ESP_DISPLAY_PRESENT_GRAM_BACKGROUND` optionally submits synchronous
internal-tile GRAM work on the backend worker outside the engine gate. It is off
by default; its priority is configured separately. Whole framebuffer/copy object
placement in IRAM is not forced; explicit ISR/pixel `IRAM_ATTR` annotations remain.

[Host regression and target smoke instructions](dev/README.md) describe the
maintained test entry points. Performance measurements and raw hardware evidence
belong to the MR validation attachments, with their tested source/configuration;
they are not a claim about every later revision. Receipt completion and pixel
checks alone do not establish visually tear-free output.
