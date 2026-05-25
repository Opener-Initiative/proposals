Tools in hophop
===============

* The `RSSI demo <https://github.com/ariel-os/hophop/tree/main/examples#running-the-rssi-example>`__
  contains all tools to continuously scan RSSI on a channel
  (or to sweep multiple channels)
  and to visualize the usage pattern in Python.

* The `ping example <https://github.com/ariel-os/hophop/tree/main/examples#running-the-ping-example>`__
  is useful in debugging timing between transmission and reception on different transceivers.

* The `ts-103-636-numbers <https://crates.io/crates/ts-103-636-numbers>`__
  and `ts-103-646-utils <https://crates.io/crates/ts-103-636-utils>`__ crates
  encapsulate the mapping between names and numbers
  in the various documents,
  mostly around MAC IEs,
  for decoding them and for debug output.

  Even outside of the Rust implementation,
  they can serve as a tool to generate code point lists,
  reducing the number of times the standards' tables need to be copied into code.
