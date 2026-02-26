## Overview

- `/light` [Set light](#light-set)
- `/light/remove` [Remove a light](#light-remove)

## Reference

### Light set

`/light`

Arguments:
- **light** (int|string)  light id
- **x** (float)
- **y** (float)
- **z** (float)

Examples:
```supercollider
~visuals.sendMsg('/light', 0, 500, 0, 500); // create a light at (500, 0, 500)
```

### Light remove

`/light/remove`

Arguments:
- **light** (int|string)  light id

Examples:
```supercollider
~visuals.sendMsg('/light/remove', 0); // remove light 0
```
