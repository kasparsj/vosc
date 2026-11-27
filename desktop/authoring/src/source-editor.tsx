import { useLayoutEffect, useRef, type RefObject } from "react";
import { EditorState, type Text } from "@codemirror/state";
import { EditorView, drawSelection, keymap, lineNumbers } from "@codemirror/view";
import { defaultKeymap, history, historyKeymap, indentLess, indentMore } from "@codemirror/commands";
import { HighlightStyle, StreamLanguage, syntaxHighlighting } from "@codemirror/language";
import { shader } from "@codemirror/legacy-modes/mode/clike";
import { tags } from "@lezer/highlight";
import { SOURCE_LIMIT } from "./contracts";

const highlighting = HighlightStyle.define([
  { tag: [tags.keyword, tags.typeName], color: "#c9adf3" },
  { tag: [tags.number, tags.bool], color: "#f0bb83" },
  { tag: tags.string, color: "#a4e5bc" },
  { tag: tags.comment, color: "#8d9bae" },
  { tag: [tags.processingInstruction, tags.definition(tags.variableName)], color: "#a1c9f4" },
]);
const glsl = StreamLanguage.define(shader);
const encoder = new TextEncoder();

export interface SourceLocation {
  document: Text;
  line: number;
  column: number;
}

export function SourceEditor({ source, editor, onChange, onPosition, onProblem, onCompositionEnd }: {
  source: string;
  editor: RefObject<EditorView | null>;
  onChange: (text: string) => void;
  onPosition: (line: number, column: number) => void;
  onProblem: (text: string, location: SourceLocation) => void;
  onCompositionEnd: () => void;
}) {
  const parent = useRef<HTMLDivElement>(null);
  const options = useRef({ source, onChange, onPosition, onProblem, onCompositionEnd });
  options.current = { source, onChange, onPosition, onProblem, onCompositionEnd };
  useLayoutEffect(() => {
    const nonce = document.querySelector<HTMLMetaElement>('meta[name="vosc-style-nonce"]')?.content;
    let mounted = true;
    const view = new EditorView({
      parent: parent.current!,
      state: EditorState.create({
        doc: options.current.source.replace(/\r\n?/g, "\n"),
        extensions: [
          ...(nonce ? [EditorView.cspNonce.of(nonce)] : []),
          EditorView.darkTheme.of(true),
          lineNumbers(), history(), drawSelection(), glsl, syntaxHighlighting(highlighting),
          keymap.of([
            { key: "Mod-]", run: indentMore },
            { key: "Mod-[", run: indentLess },
            ...historyKeymap, ...defaultKeymap,
          ]),
          EditorView.contentAttributes.of({
            "aria-label": "GLSL source", "aria-multiline": "true", spellcheck: "false",
            autocorrect: "off", autocapitalize: "off",
          }),
          EditorState.changeFilter.of(tr => {
            if (!tr.docChanged) return true;
            let firstChange: number | undefined;
            let nullChange: number | undefined;
            tr.changes.iterChanges((from, _to, _start, _end, inserted) => {
              firstChange ??= from;
              if (nullChange !== undefined) return;
              for (const line of inserted.iterLines()) {
                if (line.includes("\u0000")) {
                  nullChange = from;
                  break;
                }
              }
            });
            const exceeds = tr.newDoc.length > SOURCE_LIMIT ||
              (tr.newDoc.length * 3 > SOURCE_LIMIT && encoder.encode(tr.newDoc.toString()).length > SOURCE_LIMIT);
            if (nullChange === undefined && !exceeds) return true;
            const document = tr.startState.doc;
            const position = nullChange ?? firstChange ?? tr.startState.selection.main.head;
            const line = document.lineAt(position);
            queueMicrotask(() => {
              if (mounted && editor.current?.state.doc === document) {
                options.current.onProblem(nullChange !== undefined
                  ? "This edit contains a null character. Remove it and paste again."
                  : "This edit exceeds the 128 KiB source limit. Reduce it and try again.",
                { document, line: line.number, column: position - line.from + 1 });
              }
            });
            return false;
          }),
          EditorView.domEventHandlers({
            compositionend() {
              options.current.onCompositionEnd();
              return false;
            },
          }),
          EditorView.updateListener.of(update => {
            const cursor = update.state.selection.main.head;
            const line = update.state.doc.lineAt(cursor);
            if (update.selectionSet || update.docChanged) {
              options.current.onPosition(line.number, cursor - line.from + 1);
            }
            if (update.docChanged) options.current.onChange(update.state.doc.toString());
          }),
        ],
      }),
    });
    editor.current = view;
    options.current.onPosition(1, 1);
    return () => {
      mounted = false;
      if (editor.current === view) editor.current = null;
      view.destroy();
    };
  }, [editor]);

  return <div ref={parent} className="source-editor" />;
}
