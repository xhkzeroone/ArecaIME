{ pkgs ? import <nixpkgs> { } }:

pkgs.mkShell {
  packages = with pkgs; [
    cmake
    ninja
    pkg-config
    extra-cmake-modules
    go
    git
    fcitx5
    libinput
    systemd
    dbus
    SDL3
    fontconfig
    gtk4
    gdk-pixbuf
    (python3.withPackages (ps: [ ps.pygobject3 ]))
    libei
    libportal
    wayland
    wayland-scanner
  ];
}
