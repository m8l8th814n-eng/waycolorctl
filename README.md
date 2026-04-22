# A little program I needed for my new Lenovo V15 G4 AMN 
..
because I do not want to go with the hyprhype and I really needed the Hyprland Visal Editor in MangoWM. 
..
waycolor --contrast 1.2
..

 (If u use a lot of transparent windows u need to tweak that after)
![way](https://github.com/m8l8th814n-eng/waycolorctl/blob/main/waycolorctl.gif)
`waycolorctl` is a small Wayland command-line tool for adjusting output gamma ramps on wlroots-based compositors.

It is intended for quick display tuning when you want more control than a simple color-temperature daemon, but without writing a full compositor plugin.

## What It Can Change

- brightness
- contrast
- gamma
- red channel balance
- green channel balance
- blue channel balance

## Current Feature Set

As of now, `waycolorctl` supports:

- listing detected Wayland outputs
- selecting one output by index
- selecting one output by exact output name
- selecting one output by exact output description
- applying the same tuning to all outputs when no output is specified
- per-channel RGB gain control
- brightness offset
- contrast multiplier
- gamma exponent adjustment
- neutral ramp reset with `--reset`
- automatic restore when the process exits
- `SIGINT` and `SIGTERM` handling so `Ctrl-C` or a normal terminate restores defaults
- output summaries with name, description, resolution, refresh rate, and scale when available

## What It Cannot Change

`waycolorctl` does not provide true compositor-wide saturation control.

The Wayland `wlr-gamma-control` protocol only exposes gamma ramps, so this tool approximates display tuning through transfer curves. In practice, that is enough for many laptop-panel adjustments, but it is not the same as full color grading.

## Requirements

- A Wayland session
- A wlroots-based compositor
- Compositor support for `zwlr_gamma_control_manager_v1`
- No other process currently holding gamma control for the same output

## Build

```bash
make
```

The binary will be created at:

```bash
./build/waycolorctl
```

## Usage

List detected outputs:

```bash
./build/waycolorctl --list
```

Apply settings to all outputs:

```bash
./build/waycolorctl --contrast 1.08 --brightness -0.02 --red 1.03 --blue 0.98
```

Apply settings to one output by name or index:

```bash
./build/waycolorctl --output eDP-1 --contrast 1.10
./build/waycolorctl --output 0 --gamma 0.95
```

Reset to identity ramps:

```bash
./build/waycolorctl --reset
```

Show help:

```bash
./build/waycolorctl --help
```

## Command-Line Options

```text
-l, --list
    List detected outputs and exit

-o, --output NAME|INDEX
    Select a single output by exact name, exact description, or numeric index

--brightness VALUE
    Brightness offset, approximately in the range -1.0 to 1.0
    Default: 0.0

--contrast VALUE
    Contrast multiplier
    Default: 1.0

--gamma VALUE
    Gamma exponent control
    Default: 1.0

--red VALUE
    Red channel multiplier
    Default: 1.0

--green VALUE
    Green channel multiplier
    Default: 1.0

--blue VALUE
    Blue channel multiplier
    Default: 1.0

--reset
    Apply neutral ramps

-h, --help
    Show help text
```

## Important Runtime Behavior

The process must remain running while the adjustment is active.

This is how gamma control works with the protocol: the client keeps ownership of the gamma ramp, and when the client exits, the compositor restores the default ramps.

- Keep it running in a terminal
- Or start it in the background from your compositor config
- Press `Ctrl-C` to restore default ramps

The tool restores defaults when:

- you press `Ctrl-C`
- it receives `SIGTERM`
- you press 'Ctrl-Z' it goes to background and you can kill it !

## Output Selection

`--output` accepts either:

- an output index from `--list`
- the exact output name
- the exact output description

Example:

```bash
./build/waycolorctl --list
./build/waycolorctl --output 0 --contrast 1.06
```

You can also match by full output description if your compositor provides one.

When you run `--list`, the tool prints:

- output index
- output name
- output description when available
- make and model as fallback metadata
- current resolution
- current refresh rate when available
- output scale

## Lenovo V15 G4 AMN 82YU Notes

This setup is specifically intended for:

- Lenovo V15 G4 AMN 82YU
- AMD Ryzen 5 7520U
- Radeon Graphics

On this laptop, `waycolorctl` is useful for panel tuning such as:

- slightly warmer image balance
- reducing harsh blue tint
- light contrast correction
- small brightness compensation without changing compositor theme settings

A reasonable starting point for this machine is:

```bash
./build/waycolorctl --contrast 1.08 --brightness -0.02 --red 1.03 --blue 0.98
```

If your internal panel is exposed as `eDP-1`, you can target only that display:

```bash
./build/waycolorctl --output eDP-1 --contrast 1.08 --brightness -0.02 --red 1.03 --blue 0.98
```

Always confirm the actual output name first:

```bash
./build/waycolorctl --list
```

## Common Failure: "Failed to acquire"

If you see:

```text
Failed to acquire
```

it usually means another tool already owns gamma control for that output.

Common conflicts include:

- `wlsunset`
- `hyprsunset`
- another running `waycolorctl` instance

Because gamma control is exclusive per output, only one program can hold it at a time.

Typical fix:

```bash
pkill wlsunset
pkill hyprsunset
pkill waycolorctl
./build/waycolorctl --contrast 1.08 --brightness -0.02 --red 1.03 --blue 0.98
```

## Example Commands

List outputs:

```bash
./build/waycolorctl --list
```

Apply to every detected output:

```bash
./build/waycolorctl --contrast 1.05 --gamma 0.97
```

Apply to one output by index:

```bash
./build/waycolorctl --output 0 --brightness -0.03 --contrast 1.08
```

Apply to one output by name:

```bash
./build/waycolorctl --output eDP-1 --red 1.03 --green 1.00 --blue 0.98
```

Apply a warmer laptop-panel profile:

```bash
./build/waycolorctl --output eDP-1 --brightness -0.02 --contrast 1.08 --red 1.03 --blue 0.98
```

Return everything to neutral:

```bash
./build/waycolorctl --reset
```

## Exit Status and Error Cases

`waycolorctl` returns a non-zero exit status when:

- it cannot connect to the Wayland display
- no outputs are found
- the compositor does not expose gamma-control support
- no output matches `--output`
- gamma control cannot be acquired
- the compositor rejects the gamma ramp
- a Wayland roundtrip or dispatch fails

## Notes

- Works only if the compositor supports `zwlr_gamma_control_manager_v1`
- Gamma control is exclusive per output
- Destroying the client restores the original ramps
- `--reset` applies neutral ramps before the tool exits
