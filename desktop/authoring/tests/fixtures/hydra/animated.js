function wave(frequency, sync, offset, angle, speed) {
  let node = osc(frequency, sync, offset);
  for (const step of [angle, 9]) {
    node = node.rotate(step, speed);
  }
  return node;
}

wave(11, 0.18, 0.7, 23, 0.15)
  .blend(wave(7, -0.12, 1.1, -31, -0.2), param("mix", 0.5))
  .rotate(17)
  .out();
