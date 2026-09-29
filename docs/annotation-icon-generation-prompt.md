# Interactive design prompt: seven Snitt annotation icons

Help me design a cohesive set of seven custom toolbar icons for **Snitt**, a desktop screenshot annotation app. Work with me interactively before creating the full set. This brief is self-contained; no files or prior conversation are required.

## Visual direction

I like colored icons with restrained depth. My latest direction is to make Cut, Rectangle, and Highlight **miniature gesture diagrams** showing where a drag starts, where it ends, and the affected area. These diagrams replace conventional scissors or physical-marker imagery rather than adding more elements to it. The remaining icons use the illustrated concepts below, harmonized through palette, weight, scale, and lighting.

- Design for **26 × 26 logical pixels**. Show actual-size previews as well as enlarged artwork.
- Use transparent artwork that reads clearly on the dark toolbar `#1b1e23` and selected-button background `#283b50`.
- Use crisp silhouettes, simple color areas, consistent padding, and equal apparent weight. Keep gesture geometry front-facing.
- Suggest depth with a restrained upper-left highlight or shaded edge. Avoid glossy glare, heavy shadows, tiny textures, and perspective distortion.
- Where specified, a **hollow circle means start** and a **solid circle means endpoint**. Keep the difference visible at 26 px. If these resemble resize handles, propose the smallest useful directional cue.
- Tool colors illustrate objects and affected regions. The app separately has configurable Good/Bad drawing colors; switching mode should not recolor these icons.
- Selection is shown by the surrounding button background/border. Do not bake buttons, labels, or shortcut letters into the artwork. The Text icon's T is intentional.

## Icon concepts

### 1. Cut

**Behavior:** a sideways drag removes a full-height vertical strip; an up/down drag removes a full-width horizontal strip. Remaining edges join. This is not clipboard cut or ordinary crop.

**My requested imagery:** hollow circle at left-middle for the start; solid circle at the horizontal and vertical center for the endpoint; dotted vertical line through the endpoint; a rectangular removed region filled with faint red diagonal slash-line hatching.

**Interpretation to confirm:** show a restrained outlined image area. Hatch the full-height band **between the start point's x-position and the center endpoint's x-position**. Keep both markers visible above the hatch/boundary. Depict this vertical-strip case only. Use a few widely spaced hatch marks and dots that remain visible at 26 px. Do not add scissors or the earlier idea of inward joining arrows unless the sample proves unclear.

### 2. Rectangle

**Behavior:** dragging between opposite corners draws an unfilled rectangle.

**My requested imagery:** blue hollow rectangular frame; hollow circle at top-left starting corner; solid circle at bottom-right endpoint. Keep the interior empty. A subtle brighter upper edge may suggest depth, but the outline and gesture markers are primary. Check that markers communicate drawing rather than resizing before adding another cue.

### 3. Highlight

**Behavior:** dragging fills a rectangular area with translucent highlighting; currently yellow in Good mode and red in Bad mode. Overlaps accumulate.

**My requested imagery:** a point at the top-left indicating the start, plus a little yellow rectangular streak.

**Suggested elaboration:** make the point hollow to match the gesture language, with a short translucent yellow swash extending rightward and downward. Start with this simple diagram. Include a physical marker body only if it improves recognition without crowding. Yellow describes the tool, not every possible output color.

### 4. Text

**Behavior:** clicking places a multiline text-entry area; annotation size and color are adjustable.

**Proposed appearance:** solid ivory **T** with a subtle slate side edge, like a small dimensional letter. Keep the silhouette bold and simple. It need not reproduce the application's annotation typeface exactly.

### 5. Arrow

**Behavior:** dragging draws an arrow from start to endpoint.

**Proposed appearance:** blue diagonal arrow pointing upper-right, filled head, restrained lighter upper edge. Match Rectangle's blue and weight. Keep shaft/head distinct. Avoid an enclosing box suggesting an external link. Gesture circles are unnecessary unless we decide otherwise.

### 6. Pixelate

**Behavior:** replaces the selected area with coarse blocks of averaged image color; the wheel adjusts block size.

**Proposed appearance:** borderless mosaic of about five to seven filled square tiles in blue, amber, and lavender, mixing larger/smaller squares. Clear gaps, irregular outer silhouette, subtle shading. Avoid enclosing frames, grid lines, or a uniform 3×3 table arrangement.

### 7. Smart erase

**Behavior:** fills a dragged rectangle with the exact color sampled at the starting point, including transparency. This is a flat sampled-color fill, not generative removal.

**Proposed appearance:** pale pink rubber eraser with cream sleeve, touching the end of a small mark. Broad recognizable shape, modest shading, and sufficient contrast on the dark toolbar. No AI sparkles or suggestion of reconstructed image detail.

## Interactive workflow

1. First summarize the direction briefly. Ask one focused question: **does Cut hatch the full-height band between the left start and center endpoint, as interpreted above?** Ask more only for essential ambiguity.
2. After clarification, develop coordinated **Cut, Rectangle, and Highlight** samples to test the gesture language. Offer at most two coherent treatments if useful.
3. Show enlarged and 26 px previews on normal/selected toolbar backgrounds, with labels outside the artwork. Identify disappearing details or confusing markers. Let me refine and approve the direction.
4. Create the remaining icons using approved samples as references. Keep palette, lighting, padding, scale, and depth consistent. Review all seven together and refine mismatches.
5. After approval, deliver seven separate transparent assets and a labeled contact sheet. Use a consistent square master canvas, such as 512 × 512, and matching padding. Names: `cut`, `rectangle`, `highlight`, `text`, `arrow`, `pixelate`, `smart-erase`. Transparency must be real alpha.

## Formats and scope

Editable SVG is my preferred eventual application format. Image-generator output should be identified as raster concepts or transparent PNGs. Do not call a bitmap wrapped in SVG an editable vector. After approval, discuss PNG delivery versus a faithful vector redraw according to your actual capabilities. Include actual-size and high-DPI previews.

Good/Bad mode controls and Save/Copy buttons are outside these seven artwork assets.
