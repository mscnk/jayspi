# jayspi

Tool to turn a J-Link device into a SPI master.


# Requirements

  - glib-2.0
  - libjaylink


# Building

    $ make


# Example usage

Transfer the bytes `0x23` and `0x05` to a SPI slave:

    $ echo -n -e "\x23\x05" | ./jayspi
