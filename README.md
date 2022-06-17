# osc_vj
OSC VJing software built on openFrameworks

# Commands

## Setup

### Setup layers

`"/layers"`

Arguments:
- [**numLayers**](#layout) (int)
- layout (int|string)

Examples:
```
// sc
~visuals.sendMsg('/layers', 9, "grid");
9.do { |i|
	~visuals.sendMsg('/load', i, "black_hole.jpeg");
};
```

### Change layout

`"/layout"`

Arguments:
- [**layout**](#layout) (int|string)
- numLayers (int)

## Enumerations

### Layout
1. `"col"`
2. `"row"`
3. `"grid"`
4. `"stack"`
