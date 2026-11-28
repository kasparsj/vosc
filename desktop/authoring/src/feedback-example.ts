import type { LibraryItem } from "./contracts";
import { sourceWithText } from "./draft-project";

export const feedbackExample = {
  id: "example:self-feedback",
  path: "Feedback Echo",
  origin: "Built-in Hydra example",
  example: true,
} satisfies LibraryItem;

export function feedbackExampleSource() {
  return sourceWithText({
    name: feedbackExample.path, origin: feedbackExample.origin, digest: "", base64: "",
  }, `// Run to seed the echo from transparent black.
// More trail keeps more of the previous output.
const trail = param("trail", 0.91);

osc(18, 0.22, 2.1)
  .blend(src(o0).rotate(3), trail)
  .out(o0);
`);
}
