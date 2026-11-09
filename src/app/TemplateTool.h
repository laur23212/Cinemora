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
//   dafin --template-guide [--out <file>]
//       Prints the authoring reference generated from this build: binding paths, animation
//       presets and their params, text shader paints, effects and their parameters, fonts.
int runTemplateTool(int argc, char *argv[]);

} // namespace drift
