# Vault — SVG Icon Reference

All icons share a `0 0 100 100` viewBox, monoline/Apple-glyph style, and use `currentColor` for stroke/fill so they inherit color from their parent context (dark on light glass, white on colored backgrounds, etc). This doc lists every icon currently in the UI: what it's for, where it's used, and its exact markup.

---

## 1. `faceId`
**Purpose:** Face ID authentication method icon.
**Used in:** Auth overlay method picker (`btnFace`), and inside the Face ID scan panel (`iconFaceInline`) during the scanning animation.
**Style:** Stroke only, 6px weight, rounded caps/joins. Corner brackets (TrueDepth scan frame) + minimal face glyph (eyes, J-nose, smile arc).

```svg
<svg viewBox="0 0 100 100" fill="none" stroke="currentColor" stroke-width="6" stroke-linecap="round" stroke-linejoin="round">
  <path d="M 25 12 L 15 12 A 3 3 0 0 0 12 15 L 12 25" />
  <path d="M 75 12 L 85 12 A 3 3 0 0 1 88 15 L 88 25" />
  <path d="M 12 75 L 12 85 A 3 3 0 0 0 15 88 L 25 88" />
  <path d="M 88 75 L 88 85 A 3 3 0 0 1 85 88 L 75 88" />
  <line x1="38" y1="36" x2="38" y2="44" />
  <line x1="62" y1="36" x2="62" y2="44" />
  <path d="M 50 36 L 50 54 A 4 4 0 0 0 54 58 L 56 58" />
  <path d="M 33 67 A 18 18 0 0 0 67 67" />
</svg>
```

---

## 2. `touchId`
**Purpose:** Touch ID / fingerprint authentication method icon.
**Used in:** Auth overlay method picker (`btnFinger`), and inside the Fingerprint scan panel (`iconFingerInline`) during the scanning animation.
**Style:** Stroke only, 5px weight, rounded caps. Concentric ridge arcs plus swirl/baseline detail lines for a realistic fingerprint look.

```svg
<svg viewBox="0 0 100 100" fill="none" stroke="currentColor" stroke-width="5" stroke-linecap="round">
  <path d="M 45 58 C 45 42, 55 42, 55 58 L 55 64" />
  <path d="M 37 65 C 37 34, 63 34, 63 60 L 63 72" />
  <path d="M 29 68 C 29 26, 71 26, 71 56 L 71 80" />
  <path d="M 21 70 C 21 18, 79 18, 79 52 L 79 76" />
  <path d="M 15 54 C 15 36, 25 24, 38 20" />
  <path d="M 17 38 C 23 20, 42 12, 50 12" />
  <path d="M 13 65 C 13 48, 14 44, 20 34" />
  <path d="M 85 44 C 85 28, 73 16, 58 13" />
  <path d="M 87 58 C 87 46, 82 38, 76 30" />
  <path d="M 27 82 C 32 78, 38 78, 44 82" />
  <path d="M 32 90 C 38 86, 46 86, 52 90" />
</svg>
```

---

## 3. `pin`
**Purpose:** PIN authentication method icon — represents a numeric keypad.
**Used in:** Auth overlay method picker (`btnPin`).
**Style:** Filled, no stroke. 3×3 grid of solid dots, 5px radius each.

```svg
<svg viewBox="0 0 100 100" fill="currentColor">
  <circle cx="28" cy="28" r="5" /><circle cx="50" cy="28" r="5" /><circle cx="72" cy="28" r="5" />
  <circle cx="28" cy="50" r="5" /><circle cx="50" cy="50" r="5" /><circle cx="72" cy="50" r="5" />
  <circle cx="28" cy="72" r="5" /><circle cx="50" cy="72" r="5" /><circle cx="72" cy="72" r="5" />
</svg>
```

---

## 4. `password`
**Purpose:** Password authentication method icon — stylized key.
**Used in:** Auth overlay method picker (`btnPassword`). Shown at reduced opacity (`.unset` class) when no password has been configured yet.
**Style:** Stroke 5px, rounded. Circular key head with filled center pin, diagonal shaft, two teeth notches.

```svg
<svg viewBox="0 0 100 100" fill="none" stroke="currentColor" stroke-width="5" stroke-linecap="round" stroke-linejoin="round">
  <circle cx="36" cy="46" r="16" /><circle cx="36" cy="46" r="4" fill="currentColor" />
  <path d="M 50 54 L 82 76" />
  <line x1="70" y1="68" x2="77" y2="58" /><line x1="77" y1="73" x2="84" y2="63" />
</svg>
```

---

## 5. `lockClosed`
**Purpose:** Indicates a locked app/state.
**Used in:** Top-bar brand mark (`iconLockClosed`), and inside each app card's status pill when `locked: true`.
**Style:** Stroke 5px. Closed shackle arching down into the lock body, with a keyhole (filled dot + stem line) detail.

```svg
<svg viewBox="0 0 100 100" fill="none" stroke="currentColor" stroke-width="5" stroke-linecap="round" stroke-linejoin="round">
  <path d="M 34 45 L 34 34 A 16 16 0 0 1 66 34 L 66 45" />
  <rect x="22" y="45" width="56" height="40" rx="10" />
  <circle cx="50" cy="61" r="3" fill="currentColor" /><line x1="50" y1="64" x2="50" y2="71" stroke-width="4" />
</svg>
```

---

## 6. `lockOpen`
**Purpose:** Indicates an unlocked/open app state.
**Used in:** Each app card's status pill when `locked: false`.
**Style:** Same lock body as `lockClosed`, but the shackle is shifted left/up and only anchored on one side — the "open padlock" position.

```svg
<svg viewBox="0 0 100 100" fill="none" stroke="currentColor" stroke-width="5" stroke-linecap="round" stroke-linejoin="round">
  <path d="M 18 37 L 18 26 A 16 16 0 0 1 50 26 L 50 37" />
  <rect x="22" y="45" width="56" height="40" rx="10" />
  <circle cx="50" cy="61" r="3" fill="currentColor" /><line x1="50" y1="64" x2="50" y2="71" stroke-width="4" />
</svg>
```

---

## 7. `check`
**Purpose:** Success/unlocked confirmation — self-contained (includes its own colored disc background, doesn't inherit `currentColor`).
**Used in:** The post-auth success state (`checkPop`), where the `.tick` path is animated via `stroke-dasharray`/`stroke-dashoffset` to "draw" itself in.
**Style:** Solid green (`#34C759`) circle, white checkmark stroke, 6px weight.

```svg
<svg viewBox="0 0 100 100" fill="none">
  <circle cx="50" cy="50" r="44" fill="#34C759" />
  <path class="tick" d="M 32 52 L 44 64 L 68 38" stroke="#FFFFFF" stroke-width="6" stroke-linecap="round" stroke-linejoin="round" />
</svg>
```

---

## 8. `x`
**Purpose:** Close/dismiss/error glyph — self-contained colored variant.
**Used in:** Base definition for a close action; in the live UI the stats-drawer close button strips the red fill and swaps to `currentColor` (`#closeStatsBtn` does a `.replace()` on this markup) so it sits neutrally on glass instead of as a red error badge.
**Style:** Solid red (`#FF3B30`) circle, white X stroke, 6px weight — same construction as `check` for visual pairing.

```svg
<svg viewBox="0 0 100 100" fill="none">
  <circle cx="50" cy="50" r="44" fill="#FF3B30" />
  <path d="M 35 35 L 65 65 M 65 35 L 35 65" stroke="#FFFFFF" stroke-width="6" stroke-linecap="round" stroke-linejoin="round" />
</svg>
```

---

## 9. `chevronBack`
**Purpose:** Back navigation / directional glyph. Reused (horizontally flipped via CSS `scaleX(-1)`) as the keypad's delete/backspace key.
**Used in:** PIN keypad delete key (`.key.del`).
**Style:** Stroke 6px, single open angle bracket.

```svg
<svg viewBox="0 0 100 100" fill="none">
  <path d="M 62 24 L 36 50 L 62 76" stroke="currentColor" stroke-width="6" stroke-linecap="round" stroke-linejoin="round" />
</svg>
```

---

## 10. `gear`
**Purpose:** Settings entry point.
**Used in:** Top-bar settings icon button (`iconGearBtn`).
**Style:** Stroke 5.5px. Center hub circle + 8-tooth gear outline built as one compound path (outer ring plus 8 tooth notches).

```svg
<svg viewBox="0 0 100 100" fill="none" stroke="currentColor" stroke-width="5.5" stroke-linecap="round" stroke-linejoin="round">
  <circle cx="50" cy="50" r="10" />
  <path d="M 50 24 A 26 26 0 1 1 49.9 24 Z M 44 24 L 42 12 L 58 12 L 56 24 M 68.4 34.1 L 76.9 25.6 L 88.2 36.9 L 79.7 45.4 M 76 56 L 88 58 L 88 42 L 76 44 M 65.9 68.4 L 74.4 76.9 L 63.1 88.2 L 54.6 79.7 M 44 76 L 42 88 L 58 88 L 56 76 M 31.6 65.9 L 23.1 74.4 L 11.8 63.1 L 20.3 54.6 M 24 44 L 12 42 L 12 58 L 24 56 M 34.1 31.6 L 25.6 23.1 L 36.9 11.8 L 45.4 20.3" />
</svg>
```

---

## 11. `clock`
**Purpose:** Activity/session-history entry point.
**Used in:** Top-bar activity icon button (`iconClockBtn`), and the Activity drawer header (`iconClockHeader`).
**Style:** Stroke 5px. Circle dial, center pin dot, hour hand (short, pointing left) and minute hand (long, pointing up).

```svg
<svg viewBox="0 0 100 100" fill="none" stroke="currentColor" stroke-width="5" stroke-linecap="round" stroke-linejoin="round">
  <circle cx="50" cy="50" r="44" /><circle cx="50" cy="50" r="2.5" fill="currentColor" />
  <path d="M 50 50 L 32 50" stroke-width="6" /><path d="M 50 50 L 50 20" />
</svg>
```

---

## 12. `warning`
**Purpose:** Failed-attempt / unset-credential indicator.
**Used in:** Inline next to "You haven't set up a password yet" in the auth overlay (`iconWarnInline`), and next to non-zero failed-attempt counts in the Activity drawer (`fail-badge`).
**Style:** Stroke 5.5px. Rounded-corner warning triangle, exclamation stem + filled dot.

```svg
<svg viewBox="0 0 100 100" fill="none" stroke="currentColor" stroke-width="5.5" stroke-linecap="round" stroke-linejoin="round">
  <path d="M 43.6 15.3 L 12.8 68.7 C 10.3 73 13.4 78.5 18.4 78.5 L 81.6 78.5 C 86.6 78.5 89.7 73 87.2 68.7 L 56.4 15.3 C 53.9 11 46.1 11 43.6 15.3 Z" />
  <line x1="50" y1="36" x2="50" y2="54" stroke-width="6" /><circle cx="50" cy="65" r="3" fill="currentColor" stroke="none" />
</svg>
```

---

## 13. `plus`
**Purpose:** Add-item glyph (defined, not yet wired into a button in the current build — reserved for "manually add an app to the watch-list" in Settings).
**Used in:** Not yet placed in the live UI. Add to Settings panel when that screen is built.
**Style:** Stroke 6px, simple cross.

```svg
<svg viewBox="0 0 100 100" fill="none">
  <path d="M 50 22 L 50 78 M 22 50 L 78 50" stroke="currentColor" stroke-width="6" stroke-linecap="round" stroke-linejoin="round" />
</svg>
```

---

## 14. `eye`
**Purpose:** "Show password" toggle (defined, not yet placed — reserved for the password-entry field once that input exists in Settings/onboarding).
**Used in:** Not yet placed in the live UI.
**Style:** Stroke 5.5px. Almond-shaped eye outline + iris ring.

```svg
<svg viewBox="0 0 100 100" fill="none" stroke="currentColor" stroke-width="5.5" stroke-linecap="round" stroke-linejoin="round">
  <path d="M 16 50 C 30 26, 70 26, 84 50 C 70 74, 30 74, 16 50 Z" /><circle cx="50" cy="50" r="10" />
</svg>
```

---

## 15. `eyeSlash`
**Purpose:** "Hide password" toggle counterpart to `eye` (defined, not yet placed).
**Used in:** Not yet placed in the live UI.
**Style:** Stroke 5.5px. Segmented eye outline (broken to leave room for the slash) + broken iris arcs + diagonal slash line.

```svg
<svg viewBox="0 0 100 100" fill="none" stroke="currentColor" stroke-width="5.5" stroke-linecap="round" stroke-linejoin="round">
  <path d="M 23 44 C 31 31, 62 25, 79 43" /><path d="M 77 56 C 69 69, 38 75, 21 57" />
  <path d="M 45 42.5 A 10 10 0 0 1 58 55" /><path d="M 52 57.8 A 10 10 0 0 1 42 48" />
  <line x1="22" y1="22" x2="78" y2="78" />
</svg>
```

---

## Not custom SVG (intentionally)
- **App icons on cards / auth overlay app icon** — these come from real extracted `.exe` icons per the backend spec (`prompt.md` §1.3), not hand-drawn SVGs. The current HTML prototype uses emoji as a stand-in only because there's no real detection backend wired up yet.
- **PIN keypad number keys 0–9** — plain text characters, not icons.

## Open items for next icon pass
- `plus` and `eye`/`eyeSlash` are defined in the `ICONS` object but have no button wired to them yet — they're waiting on the Settings screen and the password-creation flow, neither of which exist in the UI yet.
- No icon yet exists for: Settings sub-items (toggle autostart, edit exclusion list, manage Hello), a generic "app" fallback icon for when icon extraction fails, or a drag-handle/reorder icon if the dashboard ever supports manual app ordering. Flag if you want these designed now or only when those screens get built.
