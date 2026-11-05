# Phrase suggestions

Suggestions use a private input buffer per active input context. A separate
Bamboo adapter converts copies of ordinary text keys with the configured input
method, spelling, capitalization, and macro settings. Its result updates only
that private buffer. The observer never reads surrounding text, restores text
into the typing engine, or modifies the typing engine's surrounding cache.
Input processing continues without the observer accepting or forwarding keys.

The buffer retains at most 2 KiB, trimmed at a word boundary. Backspace, Delete,
navigation, shortcuts, detected mouse clicks, activation, deactivation, accepted
application resets, and configuration changes discard observation state and
session learning. Backend-generated erase/navigation events during a rewrite
are excluded from observation. Password and sensitive fields, Redirect mode,
and non-Unicode output disable observation.

After completing a word (for example, typing a space after `cộng hòa`), Areca
matches the last two or more observed words. A short timer waits for the typing
scheduler to settle before displaying candidates; it never delays the scheduler.
The panel stays hidden while the real input method has an active composition.
New input hides the panel and invalidates pending candidate callbacks.
The current composing word stays out of the searchable history until a word
boundary. Backspace edits that word while preserving earlier completed context.

Pressing `Shift+Space` selects the first visible suggestion. Tab is not a
suggestion shortcut. Clicking a candidate or pressing `Alt+1` through `Alt+5`
selects the corresponding
visible suggestion and commits only its missing suffix, then starts a fresh
observation buffer. Candidates display only the missing suffix, without shortcut
labels or the already typed prefix. Shortcut instructions are shown in Settings. The
shortcut is handled only when the corresponding candidate is visible; other
modifier combinations and shortcuts for missing candidates pass through.
Acceptance first requests the ordinary input-context
reset while the typing scheduler and composition are idle, so the previous
word's Backspace recovery snapshot cannot be applied to the inserted completion.
Tab, plain numbers, arrows, and Escape keep their ordinary
input behavior. Candidate acceptance does not update the shared surrounding
cache. No suggestion is inserted merely because its prefix matches.

Contiguous phrases of two to eight words can be learned from this private buffer.
They fill remaining candidate slots after packaged phrases. Choosing a phrase
records a small per-input-context preference count and raises that phrase among
similarly matched candidates. Candidates with the same match length and choice
count rotate between updates, so a fixed five-item page does not permanently
hide the rest of a large match set. Selection counts, observed input, and learned
phrases stay in memory and are discarded when the input context is destroyed;
they are not written to disk.
`PhraseSuggestions` enables or disables the feature.
The Settings app's **Cụm từ gợi ý** page lists the packaged phrases and lets you
search, add, or remove personal entries. Personal entries need at least three
words, are saved under the user's Fcitx5 configuration directory, and are loaded
when Areca reloads its configuration.

This is a best-effort observation of input, not a mirror of the editor. Text
pasted or changed by the application, text already present before activation,
and edits made outside the observed input stream cannot be reconstructed.
Suggestions become available again after typing fresh context. Mouse movement
without a detectable click and application-specific key handling can make the
private prediction differ from the actual editor contents.

The packaged phrase list is in `data/phrase-suggestions.tsv`; source and reuse
notes are in `data/phrase-suggestions-sources.md`.
