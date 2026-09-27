# Annotation text font

Screenshot text uses the installed Impact font when available. If Impact is
missing, xshot tries the existing platform annotation font (Segoe UI on
Windows, Helvetica on macOS), then the system's general UI font. The text-entry
preview, saved annotations, enlarged exports, undo/redo replay, and text masks
share the same resolved font. Other application UI typography is unchanged.

xshot does not bundle or install fonts. Install Impact through the operating
system if you want its appearance; otherwise the documented fallback is used.
