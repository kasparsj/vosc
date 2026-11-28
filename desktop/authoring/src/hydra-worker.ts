import { HYDRA_EXCHANGE_LIMIT, HYDRA_SOURCE_LIMIT } from "./hydra-compiler";
import { evaluateHydraPatch, UnsupportedHydraOperation } from "./hydra-patch";

const encode = new TextEncoder();
const byteLength = (text: string) => encode.encode(text).length;
const stringify = JSON.stringify;

globalThis.addEventListener("message", (event: MessageEvent<unknown>) => {
  const source = event.data;
  const port = event.ports[0];
  if (!port) return;
  const send = port.postMessage.bind(port);
  try {
    if (typeof source !== "string" || byteLength(source) > HYDRA_SOURCE_LIMIT) {
      throw new Error("Hydra source exceeds 128 KiB.");
    }
    const graph = stringify(evaluateHydraPatch(source));
    if (byteLength(graph) > HYDRA_EXCHANGE_LIMIT) throw new Error("Hydra graph exceeds 64 KiB.");
    send(graph);
  } catch (error) {
    const code = error instanceof SyntaxError ? "hydra_syntax" :
      error instanceof ReferenceError || error instanceof UnsupportedHydraOperation ? "hydra_unsupported" : "hydra_evaluation";
    const message = error instanceof Error ? error.message.slice(0, 2048) : "The Hydra patch threw an error.";
    send(stringify({ error: { code, message } }));
  }
}, { once: true });
