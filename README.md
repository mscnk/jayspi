# jayspi

Tool to turn a J-Link device into a SPI master.


# Requirements

  - glib-2.0
  - libjaylink


# Building

    $ make


# Pin assignments

Pin assignments for the 20-pin JTAG connector of a J-Link device:

| JTAG pin name | JTAG pin | SPI pin name |
|---------------|----------|--------------|
| VTref         | 1        |              |
| TRST          | 3        | CS           |
| TDI           | 5        | MOSI         |
| TCK           | 9        | SCK          |
| TDO           | 13       | MISO         |

The **VTref** pin of the J-Link device must be attached to the logic level of
the SPI slave.
The J-Link device measures the voltage on this pin and generates the reference
voltage for its input comparators and adapts its output voltages to it.

# Example usage

Transfer the bytes `0x23` and `0x05` to a SPI slave:

    $ echo -n -e "\x23\x05" | ./jayspi
