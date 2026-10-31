# Input method announcements

Shared classes live in `src/notification/`.

`InputMethodAnnouncement` is an abstract Fcitx5 switch watcher. It calls
`announce(identity)` when the selected input method changes to the configured
ID. Focus changes alone do not announce. It owns and unregisters its watcher.

`DynamicIslandAnnouncement` implements the hook with a session D-Bus signal.
The shared classes contain no Areca-specific identity.

To integrate another Fcitx5 input method, add both classes' `.cpp` files to its
build, declare `dbus` as an optional addon dependency, and keep an instance alive
in the engine:

```cpp
std::unique_ptr<areca::InputMethodAnnouncement> announcement_;

// During engine construction:
announcement_ = std::make_unique<areca::DynamicIslandAnnouncement>(
    *instance, areca::InputMethodIdentity{
        "your-input-method-id", "Your input method", "your-themed-icon"});
```

The instance must outlive the announcement. Construction and destruction run on
the Fcitx event-loop thread. Missing D-Bus addon skips the announcement; the
signal does not wait for Island or require it to be installed.

For a different destination, derive from `InputMethodAnnouncement` and implement
`void announce(const InputMethodIdentity &) override`. The watcher needs no
changes.

Other frameworks can send the same session-bus signal directly:

- Path: `/com/github/dynamic_island/InputMethod`
- Interface: `com.github.dynamic_island.InputMethod`
- Signal: `Activated`
- Arguments: three strings containing ID, display name, and themed icon name
