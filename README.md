# osc_vj
OSC VJing software built on openFrameworks

# Commands

## Setup

### Layouts
1. `"col"`
2. `"row"`
3. `"grid"`
4. `"stack"`

`"/layers"`
Setup layers.
Arguments:
- **numLayers** (int)
- layout (int|string)
Examples:
```
// sc
~visuals.sendMsg('/layers', 9, "grid");
9.do { |i|
	~visuals.sendMsg('/load', i, "black_hole.jpeg");
};
```

`"/layout"`
Change layout.
Arguments:
- **layout** (int|string)
- numLayers (int)
