# fw-dpms-kblight

A tiny Linux daemon for Framework laptops to automatically toggle the keyboard
backlight when the display turns on and off

## Building and installing

    make
    sudo make install
    sudo systemctl enable --now fw-dpms-kblight.service

To specify a prefix other than `/usr/local`:

    sudo make PREFIX=/your/custom/path install

## Uninstalling

    sudo systemctl disable --now fw-dpms-kblight.service
    sudo make uninstall

Make sure you use the same `PREFIX` as you did for installing.
