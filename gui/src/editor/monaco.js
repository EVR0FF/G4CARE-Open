//==============================================================================
// G4CARE - monaco.js — Monaco setup: YAML schema wiring + snippet registration
//
// Monaco setup: YAML schema wiring + snippet registration
//
// @author I. I. Everstov  @author V. F. Myshkin (Scientific Supervisor)
// @date 2026-11-11  @version 0.9.3
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

import { configureMonacoYaml } from 'monaco-yaml';

export function configureYaml(monaco, schema) {
    configureMonacoYaml(monaco, {
        enableSchemaRequest: true,
        validate: true,
        hover: true,
        completion: true,
        format: { enable: true },
        schemas: [{ uri: 'g4care://config-schema.json', fileMatch: ['*'], schema }],
    });
}

export function registerSnippets(monaco, snippets) {
    const items = [];
    for (const [name, snip] of Object.entries(snippets)) {
        items.push({
            label: snip.prefix || name,
            kind: monaco.languages.CompletionItemKind.Snippet,
            detail: name,
            documentation: snip.description || name,
            insertText: Array.isArray(snip.body) ? snip.body.join('\n') : String(snip.body),
            insertTextRules: monaco.languages.CompletionItemInsertTextRule.InsertAsSnippet,
        });
    }
    monaco.languages.registerCompletionItemProvider('yaml', {
        provideCompletionItems: () => ({ suggestions: items }),
    });
}
