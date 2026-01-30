#!/usr/bin/env python3
"""
Nanox cognitive-style theme codegen (trait-based, not diagnostic) with a strong RGB-axis penalty.

Fixed logic for the 'error' style to prevent foreground/background color collisions.
All comments translated to English for technical consistency.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Optional, Tuple


@dataclass(frozen=True)
class Style:
    """A single style entry for Nanox .nanoxcolor."""
    fg: Optional[str] = None
    bg: Optional[str] = None
    bold: bool = False
    underline: bool = False

    def to_ini(self) -> str:
        """Render style into Nanox INI format."""
        parts = []
        if self.fg:
            parts.append(f"fg={self.fg}")
        if self.bg:
            parts.append(f"bg={self.bg}")
        if self.bold:
            parts.append("bold=true")
        if self.underline:
            parts.append("underline=true")
        return " ".join(parts)


def ask_yn(prompt: str) -> bool:
    """Ask a yes/no question and return True for yes."""
    while True:
        ans = input(f"{prompt} (y/n): ").strip().lower()
        if ans in ("y", "yes"):
            return True
        if ans in ("n", "no"):
            return False
        print("Please answer with 'y' or 'n'.")


def ask_name(prompt: str) -> str:
    """Ask a base theme name and sanitize it for filenames."""
    while True:
        raw = input(f"{prompt}: ").strip()
        if not raw:
            print("Please type a non-empty name.")
            continue
        name = "".join(ch for ch in raw if (ch.isalnum() or ch in ("-", "_"))).strip("-_")
        if not name:
            print("Name must contain at least one letter/number.")
            continue
        return name.lower()


def write_nanoxcolor(path: Path, meta: Dict[str, str], styles: Dict[str, Style]) -> None:
    """Write a .nanoxcolor file."""
    lines = []
    if meta:
        lines.append("[meta]")
        for k, v in meta.items():
            lines.append(f"{k} = {v}")
        lines.append("")
    lines.append("[styles]")

    order = [
        "normal", "selection",
        "comment", "string", "number",
        "bracket", "operator",
        "keyword", "flow", "return",
        "type", "function", "preproc",
        "escape", "control", "ternary",
        "notice", "error",
    ]
    for key in order:
        if key in styles:
            lines.append(f"{key:<9}= {styles[key].to_ini()}".rstrip())
    for key in sorted(k for k in styles.keys() if k not in set(order)):
        lines.append(f"{key:<9}= {styles[key].to_ini()}".rstrip())

    lines.append("")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def merge(base: Dict[str, Style], override: Dict[str, Style]) -> Dict[str, Style]:
    """Merge styles. Flags are ORed, fg/bg are overridden."""
    out: Dict[str, Style] = dict(base)
    for k, o in override.items():
        b = out.get(k, Style())
        out[k] = Style(
            fg=o.fg if o.fg is not None else b.fg,
            bg=o.bg if o.bg is not None else b.bg,
            bold=(b.bold or o.bold),
            underline=(b.underline or o.underline),
        )
    return out


def build_base_palettes() -> Tuple[Dict[str, Style], Dict[str, Style]]:
    """Base palettes: conservative and readable."""
    base_dark: Dict[str, Style] = {
        "normal":    Style(fg="#b0b0b0", bg="#0f111a"),
        "selection": Style(bg="#1a2230"),
        "comment":   Style(fg="#6d7480"),
        "string":    Style(fg="#d7c27a"),
        "number":    Style(fg="#d7c27a"),
        "bracket":   Style(fg="#9a9a9a"),
        "operator":  Style(fg="#b0b0b0"),
        "keyword":   Style(fg="#e0a070", bold=True),
        "flow":      Style(fg="#e0a070", bold=True),
        "return":    Style(fg="#e0a070", bold=True),
        "type":      Style(fg="#c7b6e6"),
        "function":  Style(fg="#66aebb"),
        "preproc":   Style(fg="#66aebb"),
        "escape":    Style(fg="#c7b6e6"),
        "control":   Style(fg="#b0b0b0", underline=True),
        "ternary":   Style(fg="#b0b0b0", bold=True),
        "notice":    Style(fg="#d7c27a", bold=True),
        "error":     Style(fg="#ffffff", bg="#8b0000", bold=True),
    }

    base_light: Dict[str, Style] = {
        "normal":    Style(fg="#24292e", bg="#f7f7f7"),
        "selection": Style(bg="#dbeafe"),
        "comment":   Style(fg="#6a737d"),
        "string":    Style(fg="#7a5a2a"),
        "number":    Style(fg="#1f5aa6"),
        "bracket":   Style(fg="#57606a"),
        "operator":  Style(fg="#24292e"),
        "keyword":   Style(fg="#7a2f2f", bold=True),
        "flow":      Style(fg="#7a2f2f", bold=True),
        "return":    Style(fg="#7a2f2f", bold=True),
        "type":      Style(fg="#4a3fb5", bold=True),
        "function":  Style(fg="#005ab5", bold=True),
        "preproc":   Style(fg="#005ab5"),
        "escape":    Style(fg="#4a3fb5"),
        "control":   Style(fg="#24292e", underline=True),
        "ternary":   Style(fg="#24292e", bold=True),
        "notice":    Style(fg="#7a5a2a", bold=True),
        "error":     Style(fg="#ffffff", bg="#cc0000", bold=True),
    }

    return base_dark, base_light


def collect_trait_weights() -> Dict[str, float]:
    """Ask trait questions and produce weights in [0,1]."""
    print("Answer based on your real experience while reading/editing code.\n")

    def w_if(question: str, weight: float) -> float:
        return weight if ask_yn(question) else 0.0

    weights = {
        "overstim": w_if("Do bright/saturated highlights quickly feel overwhelming", 0.9),
        "anchors": w_if("Do you lose your place unless keywords/flow stand out clearly", 0.9),
        "distract": w_if("Do comments/secondary tokens steal attention too easily", 0.9),
        "wm_load": w_if("Do many colors/categories feel mentally heavy or tiring", 0.9),
        "num_pop": w_if("Do numbers get missed or misread unless clearly distinct", 0.9),
        "shape_stable": w_if("Does underlining make text harder to read for you", 0.9),
        "cvd_safe": w_if("Should signals avoid relying on hue alone (CVD-friendly)", 0.9),
        "eye_strain": w_if("Do you get eye strain/headaches from harsh contrast", 0.9),
    }

    if ask_yn("Do you want calm visuals but still strong structure cues (both)"):
        weights["overstim"] = max(weights["overstim"], 0.6)
        weights["anchors"] = max(weights["anchors"], 0.6)

    return weights


def _ansi_truecolor_block(r: int, g: int, b: int, width: int = 14) -> str:
    """Return a colored block using ANSI truecolor background."""
    return f"\x1b[48;2;{r};{g};{b}m" + (" " * width) + "\x1b[0m"


def ask_rgb_salience_order() -> Tuple[bool, str]:
    """Show pure RGB blocks and ask user for perceived salience order."""
    print("\nRGB salience probe (pure primaries):")
    print("  R:", _ansi_truecolor_block(255, 0, 0), " (255,0,0)")
    print("  G:", _ansi_truecolor_block(0, 255, 0), " (0,255,0)")
    print("  B:", _ansi_truecolor_block(0, 0, 255), " (0,0,255)")
    probe_ok = ask_yn("Do the blocks look different from each other (not all the same)?")

    print("\nPlease rank them from strongest to weakest, as a 3-letter order.")
    print("Example: BGR means Blue is strongest, then Green, then Red.")
    while True:
        s = input("Order (RGB permutation): ").strip().upper()
        if len(s) == 3 and set(s) == {"R", "G", "B"}:
            return probe_ok, s
        print("Please type exactly one of each: R, G, B (e.g., BGR, GRB, ...).")


def pick_free_theme_name(dir_path: Path, desired: str) -> str:
    """Pick a non-colliding base name for dark/light pair."""
    def taken(name: str) -> bool:
        d = dir_path / f"{name}-dark.nanoxcolor"
        l = dir_path / f"{name}-light.nanoxcolor"
        return d.exists() or l.exists()

    if not taken(desired):
        return desired

    i = 1
    while True:
        candidate = f"{desired}-{i}"
        if not taken(candidate):
            return candidate
        i += 1


def palette_for_penalty_axis(axis: str) -> Dict[str, str]:
    """Return an alternate palette that strongly avoids the penalty axis."""
    if axis == "B":
        return {
            "warm":    "#d1a06f",
            "cool":    "#73b07a",
            "num":     "#e0c16a",
            "str":     "#caa76d",
            "type":    "#c7b6e6",
            "err_fg":  "#ffffff",
            "err_bg":  "#8b0000",
        }
    if axis == "G":
        return {
            "warm":    "#d6a07a",
            "cool":    "#7aa7d9",
            "num":     "#7aa7d9",
            "str":     "#caa76d",
            "type":    "#c7b6e6",
            "err_fg":  "#ffffff",
            "err_bg":  "#8b0000",
        }
    # axis == "R"
    return {
        "warm":    "#caa76d",
        "cool":    "#7ed9ff",
        "num":     "#d7c27a",
        "str":     "#d7c27a",
        "type":    "#c7b6e6",
        "err_fg":  "#ffffff",
        "err_bg":  "#5f00af",
    }


def apply_global_axis_penalty(base: Dict[str, Style], axis: str, variant: str) -> Dict[str, Style]:
    """Apply a strong global rewrite based on the penalty axis."""
    pal = palette_for_penalty_axis(axis)
    o: Dict[str, Style] = {}

    if variant == "dark":
        o["keyword"] = Style(fg=pal["warm"], bold=True)
        o["flow"] = Style(fg=pal["warm"], bold=True)
        o["return"] = Style(fg=pal["warm"], bold=True)
        o["function"] = Style(fg=pal["cool"], bold=True)
        o["preproc"] = Style(fg=pal["cool"])
        o["number"] = Style(fg=pal["num"], bold=True)
        o["string"] = Style(fg=pal["str"])
        o["type"] = Style(fg=pal["type"], bold=True)
        o["operator"] = Style(fg="#b0b0b0")
        o["bracket"] = Style(fg="#9a9a9a")
        o["error"] = Style(fg=pal["err_fg"], bg=pal["err_bg"], bold=True)
    else:
        o["function"] = Style(fg="#0b57a3" if axis != "B" else "#2a6f3c", bold=True)
        o["preproc"] = Style(fg=o["function"].fg)
        o["number"] = Style(fg=o["function"].fg, bold=True)
        o["keyword"] = Style(fg="#24292e", bold=True)
        o["flow"] = Style(fg="#24292e", bold=True)
        o["return"] = Style(fg="#24292e", bold=True)
        o["string"] = Style(fg="#7a5a2a")
        o["type"] = Style(fg="#4a3fb5", bold=True)
        o["error"] = Style(fg="#ffffff", bg="#cc0000", bold=True)

    return merge(base, o)


def trait_overrides(weights: Dict[str, float], variant: str) -> Dict[str, Style]:
    """Trait-based tweaks layered after the global palette rewrite."""
    o: Dict[str, Style] = {}

    overstim = weights["overstim"]
    anchors = weights["anchors"]
    distract = weights["distract"]
    wm_load = weights["wm_load"]
    num_pop = weights["num_pop"]
    shape_stable = weights["shape_stable"]
    cvd_safe = weights["cvd_safe"]
    eye_strain = weights["eye_strain"]

    if eye_strain >= 0.6:
        if variant == "dark":
            o["normal"] = Style(fg="#aaaaaa", bg="#111420")
            o["selection"] = Style(bg="#1c2638")
        else:
            o["normal"] = Style(fg="#24292e", bg="#f3f4f6")
            o["selection"] = Style(bg="#dbeafe")

    if overstim >= 0.6:
        if variant == "dark":
            o["comment"] = Style(fg="#707782")
            o["operator"] = Style(fg="#a9a9a9")
            o["bracket"] = Style(fg="#a5a5a5")
            o["error"] = Style(fg="#ffffff", bg="#550000", bold=True, underline=True)
        else:
            o["comment"] = Style(fg="#7a8088")
            o["operator"] = Style(fg="#2f343a")
            o["bracket"] = Style(fg="#3b4048")
            o["error"] = Style(fg="#ffffff", bg="#aa0000", bold=True, underline=True)

    if distract >= 0.6:
        if variant == "dark":
            o["comment"] = Style(fg="#4f5560")
        else:
            o["comment"] = Style(fg="#8a9099")

    if anchors >= 0.6:
        if not shape_stable:
            o["flow"] = Style(underline=True)
            o["return"] = Style(underline=True)

    if wm_load >= 0.6:
        if variant == "dark":
            o["type"] = Style(fg="#b0b0b0", bold=True)
            o["operator"] = Style(fg="#b0b0b0")
        else:
            o["type"] = Style(fg="#24292e", bold=True)
            o["operator"] = Style(fg="#24292e")

    if num_pop >= 0.6 and variant == "dark":
        o["number"] = Style(bold=True)

    if cvd_safe >= 0.6:
        o["error"] = Style(underline=True)

    return o


def main() -> None:
    out_dir = Path("configs/nanox/colorscheme")
    if not out_dir.is_dir():
        print(f"Warning: output directory not found: {out_dir}")
        print("Creating directory...")
        out_dir.mkdir(parents=True, exist_ok=True)

    print("Nanox Cognitive Theme Codegen\n")
    weights = collect_trait_weights()

    probe_ok, rgb_order = ask_rgb_salience_order()
    penalty_axis = rgb_order[0]

    desired = ask_name("\nType a theme name in English (letters/numbers/-/_)")
    final_name = pick_free_theme_name(out_dir, desired)

    base_dark, base_light = build_base_palettes()

    if probe_ok:
        dark = apply_global_axis_penalty(base_dark, penalty_axis, "dark")
        light = apply_global_axis_penalty(base_light, penalty_axis, "light")
    else:
        dark = dict(base_dark)
        light = dict(base_light)

    dark = merge(dark, trait_overrides(weights, "dark"))
    light = merge(light, trait_overrides(weights, "light"))

    dark_path = out_dir / f"{final_name}-dark.nanoxcolor"
    light_path = out_dir / f"{final_name}-light.nanoxcolor"

    write_nanoxcolor(dark_path, {"name": final_name, "variant": "dark"}, dark)
    write_nanoxcolor(light_path, {"name": final_name, "variant": "light"}, light)

    print("\nGenerated:")
    print(f"  {dark_path}")
    print(f"  {light_path}")

    if final_name != desired:
        print(f"Note: '{desired}' was already taken. Used '{final_name}' instead.")

    if probe_ok:
        axis_name = {"R": "Red", "G": "Green", "B": "Blue"}[penalty_axis]
        print(f"Applied strong global penalty axis: {axis_name} (from RGB order {rgb_order})")
    else:
        print("RGB probe was marked unreliable; global axis penalty was not applied.")


if __name__ == "__main__":
    main()
