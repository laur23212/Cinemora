#pragma once

namespace drift {

// Dafin: the command-line side of title templates, for authors working outside the editor (an AI
// such as ChatGPT, a designer). No window opens.
//
//   dafin --validate-template <file|folder>... [--json] [--portable] [--out <report file>]
//       Checks template files exactly as installing them would: schema, bindings, ranges, and the
//       effects and fonts this installation has. Exit code 0 = every file can be installed,
//       1 = at least one has errors, 2 = bad arguments.
//   dafin --template-preview <file.json> --out <sheet.png> [--text "..."] [--fps N] [--duration-ms N]
//                            [--times ms,ms,...]
//       Renders the template's motion as a contact sheet (dark and light background rows, frames
//       labelled with time and frame number) with Dafin's own text painter, for an author who
//       cannot run the editor to look at. Clip effects are not drawn.
//   dafin --check-template <file.json> --out <folder> [--text "..."]
//       The whole round for a new template: the validator in normal and portable mode, every quick
//       control (binds, changes the template, reads back, leaves the others alone, survives a save,
//       resets), the timing at 24/30/60 fps on 0.4/1/3 s clips, and five contact sheets (30 and
//       60 fps, a short clip at 24 fps, two lines, long text) — written to <folder> with
//       <name>-report.md. Exit code 0 = the automated checks passed (the sheets still need a
//       person's eyes; nothing is approved), 1 = they did not, 2 = bad arguments.
//   dafin --template-guide [--out <file>]
//       Prints the authoring reference generated from this build: binding paths, animation
//       presets and their params, text shader paints, effects and their parameters, fonts.
int runTemplateTool(int argc, char *argv[]);

} // namespace drift
