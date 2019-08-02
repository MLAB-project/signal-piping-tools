# signal-piping-tools

A set of utilities for handling signal data streams from SDR receivers.  These utilities are used mainly at radio detection stations in measuring networks. 

## Installation

Get dependencies:

    $ sudo apt-get install build-essential  libusb-dev libusb-1.0-0-dev  python-setuptools  libcfitsio-dev buffer pv ntp libvolk1-dev

And compile:

    $ git clone https://github.com/MLAB-project/signal-piping-tools.git
    $ cd signal-piping-tools
    $ make

Usage: 


    $ x_fir_dec 
    x_fir_dec: usage: x_fir_dec [-b OUT_BUF_LEN] SAMP_RATE CENTER_FREQ DECIMATION TAPS_FILE

It is allowed to define filter directly instead of use TAPS_FILE. An example of filter definition: lp:256:3000.

It means: 
  * lp - low pass
  * 256 - filter depth 
  * 3000 - end of passband
  
Resulting filter width will be 6 kHz, because it has tu be 3 kHz to both sides.
