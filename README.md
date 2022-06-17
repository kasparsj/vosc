# osc_vj
OSC VJing software built on openFrameworks

# Commands

## Setup

### Setup layers

`"/layers"`

Arguments:
- **numLayers** (int)
- [layout](#layout) (int|string)

Examples:
```
// sc
~visuals.sendMsg('/layers', 8); // initializes 8 layers with stack layout
~visuals.sendMsg('/layers', 9, "grid"); // initializes 9 layers with grid layout
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
