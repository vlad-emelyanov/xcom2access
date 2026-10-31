#pragma once

// Context-aware help for the question mark key (the one global claim, src/input.cpp).
//
// The key speaks ONE page of help for the context the player is in; pressing it again within
// kRepeatWindowMs in the same context speaks the next page and wraps. Any other context, or a
// longer pause, restarts at page 1. The pages are mod-owned words (X2AccessVocab, keys
// HelpWorld1 ...), listed per context in help_pages.cpp.

namespace HelpPages {

/// Speak the help page for the current context (Narrator, Aid channel). Game tick only: it reads
/// the surfaces' published gates, never engine memory, but speaks.
void Speak();

} // namespace HelpPages
