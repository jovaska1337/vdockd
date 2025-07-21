## vdockd (Virtual Dock Daemon)

This is a simple daemon to allow manual generation of `SW_DOCK` events in
order to inform logind of docking status. It is intended to be combined
with acpid to make Thinkpad docking function.

It uses an uinput device which exposes the `SW_DOCK` event and allows
dispatching events manually (meant to be dispatched by acpid event hooks).

### update-dock-status (Thinkpad dock status helper)

vdockd defaults to the `SW_DOCK` switch being OFF (meaning that the laptop)
is undocked. The dock status needs to be set before logind runs and queries
the status from the uinput device managed by vdockd. I have chosen to omit
detecting the dock status from vdockd so it remains as generic as possible.

As the Linux ACPI dock driver does not function properly for Thinkpad docks,
the update-dock-status utility queries the dock status from EC (Embedded
Controller) and sends it to vdockd before logind runs.

update-dock-status has only been tested on a Thinkpad W520, the docked flag
may be located in a different location on different devices. Support for
querying the docked state using the ACPI dock interface in
/sys/devices/platform/dock.* could also be added in the future.

### Installing

The Makefile is setup to install and compile both executables automatically,
just type `make install`. Edit the `PREFIX` variable if you don't want to
install under /usr/local. libevdev and libsystemd (including their headers)
are required by vdockd.

Make sure to run `systemctl enable acpid vdockd update-dock-status` and reboot.
logind should pick up the uinput device and update the docking status. You
should get docking notification in the logind journal output and you can check
the docking status manually by issuing
`busctl get-property org.freedesktop.login1 /org/freedesktop/login1 org.freedesktop.login1.Manager Docked`
