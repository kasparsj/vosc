import assert from "node:assert/strict";
import { test } from "node:test";
import { compileSinglePass } from "vosc-triode-compiler";

test("compiles nested coordinate and color transforms in fresh registries", () => {
  const build: Parameters<typeof compileSinglePass>[0] = ({ osc, setFunction }) => {
    setFunction({
      name: "proofTint",
      type: "color",
      inputs: [{ name: "amount", type: "float", default: 0.65 }],
      glsl: "return vec4(_c0.r, _c0.g * amount, _c0.b * 0.6 + 0.15, _c0.a);",
    });
    osc(12, 0.21, 0.6)
      .rotate(23, 0.13)
      .modulate(osc(7, -0.08, 0.31).rotateRad(-34 * Math.PI / 180), 0.15)
      .proofTint()
      .out();
  };
  const shader = compileSinglePass(build);
  assert.deepEqual(shader, compileSinglePass(build));
  assert.deepEqual(shader.functions.map(({ name }) => name), ["osc", "rotate", "modulate", "proofTint", "rotateRad"]);
  assert.match(shader.colorExpression, /^proofTint\(osc\(rotate\(modulate\(st, osc\(rotateRad\(/);
  assert.deepEqual(shader.uniforms, []);
  assert.throws(() => compileSinglePass(({ osc }) => osc().proofTint().out()));
});

test("wraps a custom body with a signature inside a comment", () => {
  const glsl = "// A comment with vec4 tint(vec4 _c0, float amount)\nreturn _c0 * amount;";
  const shader = compileSinglePass(({ osc, setFunction }) => {
    setFunction({ name: "tint", type: "color", inputs: [{ name: "amount", type: "float", default: 0.5 }], glsl });
    osc().tint().out();
  });
  assert.equal(shader.functions.find(({ name }) => name === "tint")?.source,
    "\n  vec4 tint(vec4 _c0, float amount) {\n      " + glsl + "\n  }\n");
});

test("snapshots branches and emits one clamped uniform per used parameter", () => {
  const shader = compileSinglePass(({ osc, parameter }) => {
    const base = osc(1e-38, 0, -10);
    const mix = parameter("mix", 0.5);
    parameter("unused", 1);
    const first = osc(10).blend(base, mix);
    base.rotateDeg(90);
    first.blend(base, parameter("mix", 0.5)).out();
  });
  assert.deepEqual(shader.uniforms, [{ name: "u_hydra_mix", type: "float", value: 0.5 }]);
  assert.equal(shader.colorExpression.match(/clamp\(u_hydra_mix, 0\.0, 1\.0\)/g)?.length, 2);
  assert.equal(shader.colorExpression.match(/rotateDeg\(/g)?.length, 1);
  assert.equal(shader.colorExpression.match(/9\.999999350456404e-39/g)?.length, 2);
  const osc = shader.functions.find(({ name }) => name === "osc")!.source;
  assert.match(osc, /\(_st\.x \+ time \* sync\) \* frequency/);
  assert.doesNotMatch(osc, /offset\s*\/\s*frequency/);
});

test("bounds expression growth before nested modulations duplicate coordinates", () => {
  assert.throws(() => compileSinglePass(({ osc }) => {
    const patch = osc();
    for (let i = 0; i < 18; i++) patch.modulate(osc(), 0.2);
    patch.out();
  }), /expression exceeds 64 KiB/);
});

test("rejects missing, repeated and targeted outputs and unsupported operands", () => {
  assert.throws(() => compileSinglePass(({ osc }) => { osc(); }), /exactly one/);
  assert.throws(() => compileSinglePass(({ osc }) => {
    osc().out();
    try { osc().out(); } catch {}
  }), /exactly one/);
  assert.throws(() => compileSinglePass(({ osc }) => {
    try { osc().out(0); } catch {}
    osc().out();
  }), /exactly one/);
  assert.throws(() => compileSinglePass(({ osc }) => Reflect.apply(osc, undefined, [() => 0]).out()), /finite float32/);
  assert.throws(() => compileSinglePass(({ osc }) => Reflect.apply(osc, undefined, [[1, 2]]).out()), /finite float32/);
  assert.throws(() => compileSinglePass(({ osc, parameter }) =>
    Reflect.apply(osc, undefined, [parameter("mix", 0.5)]).out()), /finite float32/);
  assert.throws(() => compileSinglePass(({ osc }) => osc(1e-50).out()), /finite float32/);
});
