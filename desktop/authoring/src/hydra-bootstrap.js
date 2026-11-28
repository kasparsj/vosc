"use strict";

globalThis.evaluateHydra = source => new Promise((resolve, reject) => {
  const worker = new Worker("/worker.js");
  const { port1, port2 } = new MessageChannel();
  const finish = (action, value) => {
    port1.close();
    worker.terminate();
    action(value);
  };
  port1.addEventListener("message", ({ data }) => {
    if (typeof data !== "string" || new TextEncoder().encode(data).length > 64 * 1024) {
      finish(reject, new Error("Hydra evaluator result exceeds 64 KiB."));
      return;
    }
    finish(resolve, data);
  }, { once: true });
  worker.addEventListener("error", () => {
    finish(reject, new Error("Hydra worker failed to start or crashed."));
  }, { once: true });
  port1.start();
  worker.postMessage(source, [port2]);
});
